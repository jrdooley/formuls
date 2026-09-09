/*
 * OpenStageControlProcess.h
 *
 * Launches and stops the Open Stage Control server (the formuls GUI) as a
 * child process, replacing the shell commands the old Python front end used.
 *
 * The server is the node-only distribution of Open Stage Control, run with
 * the node.js binary bundled alongside it by the build script:
 *
 *   <resource root>/gui/node                 node.js binary
 *   <resource root>/gui/open-stage-control/  the o-s-c node package
 *   <resource root>/gui/_main.json           the formuls control layout
 *
 * The launch arguments mirror the original Python command line: the GUI is
 * served on port 9001 and forwards control messages to the Pd patch, which
 * listens for OSC on udp port 9000 (see the netreceive/oscparse objects in
 * pd/_main.pd). To change ports or o-s-c options, edit the argument list in
 * OpenStageControlProcess.cpp.
 *
 *
 * Making sure the server does not outlive the app
 * -----------------------------------------------
 * An earlier version of this class assumed juce::ChildProcess was enough:
 * start it, kill it in stop(), and nothing is left behind. It is not, and a
 * node process holding tcp port 9001 was surviving both Stop and Quit --
 * which then stopped the *next* run's server from binding the port.
 *
 * There are two separate ways the server gets away, and they need two
 * separate answers:
 *
 *   1. stop() ran, but the kill did not take.
 *      juce::ChildProcess::isRunning() calls waitpid(WNOHANG) and reads a
 *      -1 return as a clean exit (it leaves the status word at 0, and
 *      WIFEXITED(0) is true), so it can report "not running" for a process
 *      that is alive. stop() used to gate process.kill() on that call.
 *      It no longer does -- see OpenStageControlProcess.cpp.
 *
 *   2. stop() never ran at all.
 *      On a crash or a force-quit no destructor runs, so no amount of
 *      cleanup code in this class can help. The child is reparented to
 *      launchd (ppid 1) and keeps the port until the machine reboots.
 *      The answer to that one is killStrayServers(), which is swept at
 *      launch and again before every start() -- the next run cleans up
 *      after the run that died.
 *
 * killStrayServers() identifies servers by inspecting running processes, so
 * it is deliberately narrow about what it will kill: see isStrayServer() in
 * the .cpp. It must never behave like the old Python front end's
 * "killall node", which would take down a user's editor, Obsidian, or any
 * other node process on the machine.
 */

#pragma once

#include <JuceHeader.h>

namespace formuls
{

class OpenStageControlProcess
{
public:
    OpenStageControlProcess() = default;
    ~OpenStageControlProcess();

    /** Port the Open Stage Control GUI is served on. Used both for the
        server's --port argument and for the browser addresses shown in the
        main window, so change it here and both follow. */
    static constexpr int guiPort = 9001;

    /** UDP port the Pd patch listens on for OSC from the GUI
        (the [netreceive -u -b] / "listen 9000" objects in _main.pd). */
    static constexpr int patchOscPort = 9000;

    /** How long to wait for a server to exit after SIGTERM before sending
        SIGKILL. Open Stage Control closes its listening socket on the way
        out, so letting it go politely frees port 9001 immediately. */
    static constexpr int killTimeoutMs = 1500;

    /** Starts the Open Stage Control server using the resources under
        resourceRoot/gui. Any server left over from an earlier run is swept
        away first, so this never fails merely because the port is taken.
        Returns Result::ok() or a failure with a user-displayable message. */
    juce::Result start (const juce::File& resourceRoot);

    /** Kills the server process. Safe to call when not running, and safe to
        call twice. */
    void stop();

    bool isRunning();

    /** Kills any Open Stage Control server left behind by an earlier formuls
        run -- the case a crash or force-quit leaves, where no destructor got
        the chance to clean up. Returns how many were killed.

        Call this at launch, before starting the engine. start() also calls
        it, so a stale server never blocks a fresh one.

        Which processes count as "ours" is decided by isStrayServer() in the
        .cpp; nothing else on the machine is touched. */
    static int killStrayServers (const juce::File& resourceRoot);

    /** The web addresses the GUI can be opened at, ready to display.

        The first entry is always the loopback address, for a browser on the
        machine running formuls. Any further entries are this machine's
        addresses on the networks it is attached to -- these are the ones to
        type into an iPad or Android tablet joined to the same network.

        This enumerates the machine's own network interfaces rather than
        parsing the addresses Open Stage Control prints to its stdout, so the
        list does not depend on the wording of another program's log output.
        IPv6 is left out: the GUI is reached over IPv4 in practice, and the
        bracketed IPv6 URL form is awkward to type on a tablet. */
    static juce::StringArray getBrowserAddresses();

private:
    juce::ChildProcess process;

    /** Where this server's node binary and o-s-c package live. Remembered by
        start() so stop() can sweep with the same paths without having to be
        handed them again. */
    juce::File resourceRoot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenStageControlProcess)
};

} // namespace formuls
