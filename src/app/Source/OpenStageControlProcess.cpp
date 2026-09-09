/*
 * OpenStageControlProcess.cpp -- see OpenStageControlProcess.h for an overview,
 * and in particular for why this file goes to the trouble of enumerating the
 * machine's processes rather than trusting juce::ChildProcess to clean up.
 */

#include "OpenStageControlProcess.h"

#include <csignal>
#include <cstring>
#include <cerrno>
#include <vector>
#include <unistd.h>

#if JUCE_MAC
 #include <libproc.h>
 #include <sys/sysctl.h>
#endif

namespace formuls
{

namespace
{

//==============================================================================
// Looking at the machine's running processes.
//
// JUCE has no API for this, and the two platforms formuls targets expose it
// completely differently -- macOS through libproc/sysctl, Linux through
// /proc -- so each is wrapped here and killStrayServers() below reads the
// same on both. On any other platform these return nothing, which makes the
// sweep a no-op rather than a compile error.

juce::Array<int> getAllProcessIds()
{
    juce::Array<int> pids;

   #if JUCE_MAC
    // Called with a null buffer, proc_listpids reports the size it needs.
    const auto bytesNeeded = proc_listpids (PROC_ALL_PIDS, 0, nullptr, 0);

    if (bytesNeeded <= 0)
        return pids;

    // Ask for more room than reported: processes can start between the
    // sizing call and the fetch, and an exactly-full buffer is silently
    // truncated rather than flagged.
    std::vector<pid_t> buffer ((size_t) bytesNeeded / sizeof (pid_t) + 64);
    const auto bytesFilled = proc_listpids (PROC_ALL_PIDS, 0, buffer.data(),
                                            (int) (buffer.size() * sizeof (pid_t)));

    for (int i = 0; i < (int) ((size_t) juce::jmax (0, bytesFilled) / sizeof (pid_t)); ++i)
        if (buffer[(size_t) i] > 0)
            pids.addIfNotAlreadyThere ((int) buffer[(size_t) i]);
   #elif JUCE_LINUX
    // Every numerically-named directory under /proc is a live process.
    for (const auto& entry : juce::RangedDirectoryIterator (juce::File ("/proc"), false,
                                                            "*", juce::File::findDirectories))
    {
        const auto name = entry.getFile().getFileName();

        if (name.isNotEmpty() && name.containsOnly ("0123456789"))
            pids.addIfNotAlreadyThere (name.getIntValue());
    }
   #endif

    return pids;
}

/** The full path of the binary a process is running, or an empty string if
    this process is not allowed to look (which is the normal answer for
    anything owned by another user). */
juce::String getExecutablePath ([[maybe_unused]] int pid)
{
   #if JUCE_MAC
    char path[PROC_PIDPATHINFO_MAXSIZE] = {};

    if (proc_pidpath (pid, path, sizeof (path)) > 0)
        return juce::String::fromUTF8 (path);
   #elif JUCE_LINUX
    const juce::File link ("/proc/" + juce::String (pid) + "/exe");
    const auto target = link.getLinkedTarget();

    // getLinkedTarget() hands back the link itself when it cannot be read.
    if (target != link)
        return target.getFullPathName();
   #endif

    return {};
}

/** A process's arguments, space-joined, or an empty string if they cannot be
    read. Used only to tell one node process from another -- the executable
    path is identical for every server started from the same bundle. */
juce::String getCommandLine ([[maybe_unused]] int pid)
{
    juce::StringArray args;

   #if JUCE_MAC
    // KERN_PROCARGS2 hands back one blob: argc as an int, then the
    // executable path, then padding NULs, then argc NUL-terminated
    // arguments. There is no struct for it; it has to be walked by hand.
    int mib[3] = { CTL_KERN, KERN_PROCARGS2, pid };
    size_t size = 0;

    if (sysctl (mib, 3, nullptr, &size, nullptr, 0) != 0 || size <= sizeof (int))
        return {};

    std::vector<char> buffer (size + 1, '\0');

    if (sysctl (mib, 3, buffer.data(), &size, nullptr, 0) != 0 || size <= sizeof (int))
        return {};

    int argc = 0;
    std::memcpy (&argc, buffer.data(), sizeof (argc));

    size_t pos = sizeof (argc);

    while (pos < size && buffer[pos] != '\0') ++pos;   // step over the exec path
    while (pos < size && buffer[pos] == '\0') ++pos;   // and its trailing padding

    for (int i = 0; i < argc && pos < size; ++i)
    {
        args.add (juce::String::fromUTF8 (buffer.data() + pos));
        pos += std::strlen (buffer.data() + pos) + 1;
    }
   #elif JUCE_LINUX
    // /proc/<pid>/cmdline separates the arguments with NULs, so it cannot be
    // read as a string -- the first argument would be the whole of it.
    juce::MemoryBlock raw;

    if (! juce::File ("/proc/" + juce::String (pid) + "/cmdline").loadFileAsData (raw))
        return {};

    raw.append ("\0", 1);   // guarantee the walk below terminates
    const auto* data = static_cast<const char*> (raw.getData());

    for (size_t pos = 0; pos + 1 < raw.getSize(); pos += std::strlen (data + pos) + 1)
        args.add (juce::String::fromUTF8 (data + pos));
   #endif

    return args.joinIntoString (" ");
}

/** SIGTERM, wait, then SIGKILL. Returns true once the process is gone.

    The polite signal comes first because Open Stage Control closes its
    listening socket when it is asked to shut down; SIGKILL leaves the
    kernel to reclaim port 9001, which is slower and is what made a restart
    look like a crash. */
bool killProcessAndWait (int pid, int timeoutMs)
{
    if (::kill (pid, SIGTERM) != 0)
        return errno == ESRCH;   // already gone: the outcome we wanted anyway

    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    while (juce::Time::getMillisecondCounter() < deadline)
    {
        if (::kill (pid, 0) != 0 && errno == ESRCH)
            return true;

        juce::Thread::sleep (25);
    }

    ::kill (pid, SIGKILL);
    juce::Thread::sleep (100);

    return ::kill (pid, 0) != 0 && errno == ESRCH;
}

//==============================================================================
/** Decides whether one running process is a formuls Open Stage Control server
    left over from an earlier run, and so safe for killStrayServers() to kill.

    This is the whole safety boundary of the sweep. Every process on the
    machine is offered to it, including the user's editor, Obsidian, Claude
    and anything else that ships an embedded node -- a machine typically has
    a dozen of them. Returning true for something that is not a formuls
    server kills a program the user was using.

    @param ourNodeBinary   this build's bundled node, e.g.
                           /Applications/formuls-0.3.0-beta.app/Contents/
                           Resources/gui/node
    @param ourOscPackage   this build's o-s-c package directory, the
                           sibling "open-stage-control" folder
    @param exePath         the binary the process is running; empty when we
                           are not allowed to look
    @param commandLine     its arguments, space-joined; empty when we are not
                           allowed to look
*/
bool isStrayServer (const juce::File& ourNodeBinary,
                    const juce::File& ourOscPackage,
                    const juce::String& exePath,
                    const juce::String& commandLine)
{
    // Narrow by design: only a process running *this bundle's own* node
    // binary can match. The node embedded in the user's editor, in Obsidian,
    // or in any other app lives at a different path and can never qualify,
    // however much its arguments happen to look like ours. That rules out
    // the whole class of accident the old front end's "killall node" caused.
    //
    // The cost of drawing the line here is that a server left behind by a
    // *different* copy of formuls -- a dev build, or an older bundle -- is
    // not recognised and keeps port 9001. That case announces itself
    // clearly (start() reports the server failing to bind) whereas killing
    // the wrong process does not, so the quiet failure is the better one.
    if (exePath != ourNodeBinary.getFullPathName())
        return false;

    // Our node is only ever launched to run our own o-s-c package (see the
    // argument list in start()), so when the arguments can be read they
    // should say so. When they cannot -- sysctl and /proc both decline for
    // some processes -- the executable path has already settled the matter;
    // an otherwise certain match should not be thrown away for want of a
    // corroboration we were never going to get.
    return commandLine.isEmpty()
        || commandLine.contains (ourOscPackage.getFullPathName());
}

} // anonymous namespace

//==============================================================================
OpenStageControlProcess::~OpenStageControlProcess()
{
    stop();
}

juce::Result OpenStageControlProcess::start (const juce::File& newResourceRoot)
{
    if (process.isRunning())
        return juce::Result::ok();

    resourceRoot = newResourceRoot;

    auto guiDir  = resourceRoot.getChildFile ("gui");
    auto node    = guiDir.getChildFile ("node");
    auto oscDir  = guiDir.getChildFile ("open-stage-control");
    auto layout  = guiDir.getChildFile ("_main.json");

    if (! node.existsAsFile())
        return juce::Result::fail ("Bundled node.js binary not found: "
                                   + node.getFullPathName());
    if (! oscDir.isDirectory())
        return juce::Result::fail ("Open Stage Control package not found: "
                                   + oscDir.getFullPathName());
    if (! layout.existsAsFile())
        return juce::Result::fail ("GUI layout not found: " + layout.getFullPathName());

    // A server left over from a crashed run still holds port 9001, and the
    // one we are about to start would fail to bind it. Clear the way first.
    if (const auto swept = killStrayServers (resourceRoot); swept > 0)
        juce::Logger::writeToLog ("Cleared " + juce::String (swept)
                                  + " leftover Open Stage Control server(s) before starting");

    // Same command line the Python front end used:
    //   node open-stage-control/ --send 127.0.0.1:9000 --port 9001
    //        --load _main.json --state _formuls-default.state
    //        --client-options framerate=25 hdpi=0
    // plus --read-only, so the served GUI cannot be edited while the app runs.
    auto state = guiDir.getChildFile ("_formuls-default.state");

    juce::StringArray args { node.getFullPathName(),
                             oscDir.getFullPathName(),
                             // OSC out -> the Pd patch
                             "--send", "127.0.0.1:" + juce::String (patchOscPort),
                             // GUI served on this port
                             "--port", juce::String (guiPort),
                             "--load", layout.getFullPathName(),
                             "--read-only",
                             "--client-options", "framerate=25", "hdpi=0" };

    if (state.existsAsFile())
        args.addArray ({ "--state", state.getFullPathName() });

    if (! process.start (args))
        return juce::Result::fail ("Could not start Open Stage Control ("
                                   + node.getFullPathName() + ")");

    juce::Logger::writeToLog ("Open Stage Control started on port "
                              + juce::String (guiPort));
    return juce::Result::ok();
}

void OpenStageControlProcess::stop()
{
    // Deliberately NOT gated on isRunning(). That call reads a -1 from
    // waitpid() as a clean exit (see the header), so it can answer "not
    // running" for a live process -- and skipping the kill on the strength
    // of that is what left node servers holding port 9001. Killing a pid
    // that has already exited is harmless, so the gate bought nothing.
    process.kill();

    // Reap it, so the pid stops existing rather than lingering as a zombie
    // that the sweep below would then spend its whole timeout on.
    if (! process.waitForProcessToFinish (killTimeoutMs))
        juce::Logger::writeToLog ("Open Stage Control did not exit within "
                                  + juce::String (killTimeoutMs) + " ms");

    // Belt and braces: if the kill above did not take -- or this instance
    // never owned the server in the first place, which is what happens when
    // an earlier run crashed -- catch it by inspecting processes instead.
    if (resourceRoot.isDirectory())
        killStrayServers (resourceRoot);

    juce::Logger::writeToLog ("Open Stage Control stopped");
}

bool OpenStageControlProcess::isRunning()
{
    return process.isRunning();
}

int OpenStageControlProcess::killStrayServers (const juce::File& resourceRoot)
{
    const auto guiDir = resourceRoot.getChildFile ("gui");
    const auto ourNode = guiDir.getChildFile ("node");
    const auto ourPackage = guiDir.getChildFile ("open-stage-control");
    const auto self = (int) ::getpid();

    int killed = 0;

    for (const auto pid : getAllProcessIds())
    {
        // Never the kernel, launchd, or this very app.
        if (pid <= 1 || pid == self)
            continue;

        const auto exePath = getExecutablePath (pid);

        // A process we cannot see into is not one we are entitled to judge.
        if (exePath.isEmpty())
            continue;

        if (! isStrayServer (ourNode, ourPackage, exePath, getCommandLine (pid)))
            continue;

        juce::Logger::writeToLog ("Killing leftover Open Stage Control server, pid "
                                  + juce::String (pid) + " (" + exePath + ")");

        if (killProcessAndWait (pid, killTimeoutMs))
            ++killed;
        else
            juce::Logger::writeToLog ("  ...pid " + juce::String (pid)
                                      + " survived SIGKILL; leaving it alone");
    }

    return killed;
}

juce::StringArray OpenStageControlProcess::getBrowserAddresses()
{
    const auto port = ":" + juce::String (guiPort);

    // Always first: a browser on this machine.
    juce::StringArray addresses { "http://127.0.0.1" + port };

    for (auto& ip : juce::IPAddress::getAllAddresses (false))   // false = IPv4 only
    {
        if (ip.isNull())
            continue;

        const auto text = ip.toString();

        // 127.x is loopback (already listed above); 169.254.x is a
        // self-assigned address, which no other device can reach.
        if (text.startsWith ("127.") || text.startsWith ("169.254."))
            continue;

        addresses.addIfNotAlreadyThere ("http://" + text + port);
    }

    return addresses;
}

} // namespace formuls
