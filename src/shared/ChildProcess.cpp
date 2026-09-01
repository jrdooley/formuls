/*
 * ChildProcess.cpp -- see ChildProcess.h for the design, in particular why
 * the watchdog exists and why it is driven by a pipe rather than by signals.
 */

#include "ChildProcess.h"

#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace formuls
{

ChildProcess::~ChildProcess()
{
    kill();
}

bool ChildProcess::start (const std::vector<std::string>& argv)
{
    if (isRunning() || argv.empty())
        return false;

    std::vector<char*> cargv;
    cargv.reserve (argv.size() + 1);

    for (auto& arg : argv)
        cargv.push_back (const_cast<char*> (arg.c_str()));

    cargv.push_back (nullptr);

    pid_t newPid = -1;

    if (posix_spawn (&newPid, cargv[0], nullptr, nullptr,
                     cargv.data(), environ) != 0)
        return false;

    pid = newPid;
    startWatchdog();
    return true;
}

void ChildProcess::startWatchdog()
{
    int fds[2] = { -1, -1 };

    if (pipe (fds) != 0)
    {
        std::fprintf (stderr, "warning: could not create watchdog pipe; the "
                              "control GUI may outlive a crash\n");
        return;
    }

    // Neither end may leak into any other process we spawn: if something
    // else held the write end open, the parent's death would not produce
    // EOF and the watchdog would wait forever.
    fcntl (fds[0], F_SETFD, FD_CLOEXEC);
    fcntl (fds[1], F_SETFD, FD_CLOEXEC);

    // Read one line from stdin. A successful read means "clean shutdown,
    // stand down"; EOF (the parent died) means kill the child.
    const std::string script =
        "IFS= read -r _ || kill -TERM " + std::to_string (pid) + " 2>/dev/null";

    const char* shell = "/bin/sh";
    char* args[] = { const_cast<char*> (shell),
                     const_cast<char*> ("-c"),
                     const_cast<char*> (script.c_str()),
                     nullptr };

    // The watchdog reads the pipe as its stdin. dup2 in a spawn file action
    // produces a descriptor that is NOT close-on-exec, so it survives exec.
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init (&actions);
    posix_spawn_file_actions_adddup2 (&actions, fds[0], STDIN_FILENO);

    pid_t newPid = -1;
    const int result = posix_spawn (&newPid, shell, &actions, nullptr,
                                    args, environ);
    posix_spawn_file_actions_destroy (&actions);
    close (fds[0]);

    if (result != 0)
    {
        close (fds[1]);
        std::fprintf (stderr, "warning: could not start watchdog; the control "
                              "GUI may outlive a crash\n");
        return;
    }

    watchdogPid = newPid;
    watchdogPipe = fds[1];   // held open for the lifetime of this process
}

void ChildProcess::stopWatchdog()
{
    if (watchdogPipe >= 0)
    {
        // Tell the watchdog this is a clean shutdown, so it exits quietly
        // instead of signalling a pid we are about to terminate ourselves.
        const char quit = 'q';
        [[maybe_unused]] const auto ignored = write (watchdogPipe, &quit, 1);
        close (watchdogPipe);
        watchdogPipe = -1;
    }

    if (watchdogPid > 0)
    {
        waitpid (watchdogPid, nullptr, 0);
        watchdogPid = -1;
    }
}

bool ChildProcess::isRunning()
{
    if (pid <= 0)
        return false;

    int status = 0;

    if (waitpid (pid, &status, WNOHANG) == 0)
        return true;    // still alive (or unwaitable -- treat as gone below)

    pid = -1;           // exited and has now been reaped
    return false;
}

void ChildProcess::kill()
{
    if (! isRunning())
    {
        stopWatchdog();
        return;
    }

    // Stand the watchdog down first: from here on we are the one doing the
    // killing, and it must not signal a pid that may since have been reused.
    stopWatchdog();

    ::kill (pid, SIGTERM);

    // Give it a moment to exit cleanly, then force it.
    for (int i = 0; i < 10 && isRunning(); ++i)
        usleep (20000);   // 10 x 20 ms

    if (isRunning())
    {
        ::kill (pid, SIGKILL);
        waitpid (pid, nullptr, 0);
        pid = -1;
    }
}

} // namespace formuls
