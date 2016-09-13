/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Local.hxx"
#include "Direct.hxx"
#include "Registry.hxx"
#include "CgroupState.hxx"
#include "system/Error.hxx"

#include <utility>

int
LocalSpawnService::SpawnChildProcess(const char *name,
                                     PreparedChildProcess &&params,
                                     ExitListener *listener)
{
    pid_t pid = ::SpawnChildProcess(std::move(params), config, CgroupState());
    if (pid < 0)
        throw MakeErrno("clone() failed");

    registry.Add(pid, name, listener);
    return pid;
}

void
LocalSpawnService::SetExitListener(int pid, ExitListener *listener)
{
    registry.SetExitListener(pid, listener);
}

void
LocalSpawnService::KillChildProcess(int pid, int signo)
{
    registry.Kill(pid, signo);
}
