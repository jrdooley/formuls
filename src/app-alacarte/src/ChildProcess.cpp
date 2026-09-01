/*
 * ChildProcess.cpp -- see ChildProcess.h for an overview.
 */

#include "ChildProcess.h"

#include <csignal>
#include <spawn.h>
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
    return true;
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
        return;

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
