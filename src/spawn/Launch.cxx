/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Launch.hxx"
#include "Systemd.hxx"
#include "CgroupState.hxx"
#include "Server.hxx"
#include "system/Error.hxx"
#include "util/PrintException.cxx"

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

    CgroupState cgroup_state;

    try {
        cgroup_state =
            CreateSystemdScope("cm4all-beng-spawn.scope",
                               "The cm4all-beng-proxy child process spawner",
                               true);
    } catch (const std::runtime_error &e) {
        fprintf(stderr, "Failed to create systemd scope: ");
        PrintException(e);
    }

    /* create a new PID namespace to keep (untrusted) child processes
       contained; we need to do that after creating the systemd scope,
       because systemd would just see "PIDs=1" (because we're pid 1 in
       the new PID namespace) */
    if (unshare(CLONE_NEWPID) < 0)
        perror("Failed to create new PID namespace");

    RunSpawnServer(ctx.config, cgroup_state, ctx.fd);
    return 0;
}

pid_t
LaunchSpawnServer(const SpawnConfig &config, int fd,
                  std::function<void()> post_clone)
{
    LaunchSpawnServerContext ctx{config, fd, std::move(post_clone)};

    char stack[32768];
    auto pid = clone(RunSpawnServer2, stack + sizeof(stack),
                     CLONE_IO | SIGCHLD,
                     &ctx);
    if (pid < 0)
        throw MakeErrno("clone() failed");

    return pid;
}
