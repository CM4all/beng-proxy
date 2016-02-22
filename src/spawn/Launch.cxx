/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Launch.hxx"
#include "Server.hxx"
#include "system/Error.hxx"

#include <sched.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/signal.h>

struct LaunchSpawnServerContext {
    const SpawnConfig &config;

    int fd;

    std::function<void()> post_clone;
};

static int
RunSpawnServer2(void *p)
{
    auto &ctx = *(LaunchSpawnServerContext *)p;

    ctx.post_clone();

    const char *name = "spawn";
    prctl(PR_SET_NAME, (unsigned long)name, 0, 0, 0);

    /* ignore all signals which may stop us; shut down only when all
       sockets are closed */
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    RunSpawnServer(ctx.config, ctx.fd);
    return 0;
}

pid_t
LaunchSpawnServer(const SpawnConfig &config, int fd,
                  std::function<void()> post_clone)
{
    LaunchSpawnServerContext ctx{config, fd, std::move(post_clone)};

    char stack[32768];
    auto pid = clone(RunSpawnServer2, stack + sizeof(stack),
                     CLONE_IO,
                     &ctx);
    if (pid < 0)
        throw MakeErrno("clone() failed");

    return pid;
}
