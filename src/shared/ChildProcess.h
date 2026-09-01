/*
 * ChildProcess.h -- shared by BOTH formuls apps (src/app and
 * src/app-alacarte).
 *
 * A minimal POSIX child-process handle built on posix_spawn, with one extra
 * guarantee that matters here: THE CHILD DIES WITH THE PARENT, however the
 * parent dies.
 *
 * Why this exists rather than juce::ChildProcess: the launcher starts a
 * node.js server (Open Stage Control) that holds ports 9000/9001. If the app
 * is force-quit or crashes, an orphaned node keeps those ports and the next
 * launch's GUI silently fails to bind. juce::ChildProcess cleans up on a
 * normal quit but exposes no PID, so there is no way to attach a watchdog to
 * it -- hence one shared implementation used by both builds, so they behave
 * identically. (The trade-off, worth naming: juce::ChildProcess is
 * cross-platform and this is POSIX-only. A Windows port would wrap
 * CreateProcess plus a Job Object, which gives the same kill-on-parent-death
 * guarantee.)
 *
 * How the guarantee works
 * -----------------------
 * Signal handlers alone cannot do it: SIGKILL is uncatchable, and a hard
 * crash may never reach a handler. So instead of trying to react to our own
 * death, we let the kernel announce it:
 *
 *   1. start() creates a pipe and spawns a tiny /bin/sh watchdog whose stdin
 *      is the read end.
 *   2. The parent holds the write end open for as long as it lives.
 *   3. The watchdog blocks reading stdin. When the parent dies -- normally,
 *      killed, or crashed -- every fd it held is closed by the kernel, the
 *      pipe reaches EOF, the read fails, and the watchdog kills the child.
 *   4. On a clean stop the parent first writes one byte, so the watchdog's
 *      read *succeeds*; it then exits without killing anything, and the
 *      parent terminates the child itself.
 *
 * Both fds are marked close-on-exec so the child being guarded never inherits
 * the write end -- if it did, the pipe would never reach EOF and the whole
 * mechanism would be silently dead.
 */

#pragma once

#include <string>
#include <vector>
#include <sys/types.h>

namespace formuls
{

class ChildProcess
{
public:
    ChildProcess() = default;
    ~ChildProcess();

    ChildProcess (const ChildProcess&) = delete;
    ChildProcess& operator= (const ChildProcess&) = delete;

    /** Spawns argv[0] with the given arguments, and a watchdog that will
        terminate it if this process dies. Returns false on failure. */
    bool start (const std::vector<std::string>& argv);

    /** True while the child is alive (reaps it once it has exited). */
    bool isRunning();

    /** Terminates the child: SIGTERM, a short grace period, then SIGKILL,
        and stands the watchdog down. Safe to call when not running. */
    void kill();

private:
    void startWatchdog();
    void stopWatchdog();

    pid_t pid = -1;
    pid_t watchdogPid = -1;
    int watchdogPipe = -1;   // write end; closing it is what fires the watchdog
};

} // namespace formuls
