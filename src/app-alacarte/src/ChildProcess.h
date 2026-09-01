/*
 * ChildProcess.h -- the à la carte stand-in for juce::ChildProcess.
 *
 * A minimal POSIX child-process handle built on posix_spawn. ~70 lines of
 * implementation replace the one JUCE facility the launcher needed for
 * process management. Stopping kills exactly the process that was started
 * (first SIGTERM, then SIGKILL), never anything else on the machine.
 *
 * POSIX-only; a Windows port would wrap CreateProcess/TerminateProcess.
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

    /** Spawns argv[0] with the given arguments. Returns false on failure. */
    bool start (const std::vector<std::string>& argv);

    /** True while the child is alive (reaps it once it has exited). */
    bool isRunning();

    /** Terminates the child: SIGTERM, a short grace period, then SIGKILL.
        Safe to call when not running. */
    void kill();

private:
    pid_t pid = -1;
};

} // namespace formuls
