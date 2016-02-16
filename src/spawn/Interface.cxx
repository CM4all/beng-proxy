/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Interface.hxx"

#include <signal.h>

void
SpawnService::KillChildProcess(int pid)
{
    KillChildProcess(pid, SIGTERM);
}
