/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Local.hxx"
#include "Prepared.hxx"
#include "system/sigutil.h"
#include "system/fd_util.h"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

static void
CheckedDup2(int oldfd, int newfd)
{
    if (oldfd < 0)
        return;

    if (oldfd == newfd)
        fd_set_cloexec(oldfd, false);
    else
        dup2(oldfd, newfd);
}

gcc_noreturn
static void
Exec(const char *path, const PreparedChildProcess &p)
{
    p.refence.Apply();
    p.ns.Setup();
    p.rlimits.Apply();

    constexpr int CONTROL_FILENO = 3;
    CheckedDup2(p.stdin_fd, STDIN_FILENO);
    CheckedDup2(p.stdout_fd, STDOUT_FILENO);
    CheckedDup2(p.stderr_fd, STDERR_FILENO);
    CheckedDup2(p.control_fd, CONTROL_FILENO);

    execve(path, const_cast<char *const*>(p.args.raw()),
           const_cast<char *const*>(p.env.raw()));

    fprintf(stderr, "failed to execute %s: %s\n", path, strerror(errno));
    _exit(EXIT_FAILURE);
}

struct SpawnChildProcessContext {
    const PreparedChildProcess &params;

    const char *path;

    sigset_t signals;

    SpawnChildProcessContext(PreparedChildProcess &_params)
        :params(_params), path(_params.Finish()) {}
};

static int
spawn_fn(void *_ctx)
{
    auto &ctx = *(SpawnChildProcessContext *)_ctx;

    install_default_signal_handlers();
    leave_signal_section(&ctx.signals);

    Exec(ctx.path, ctx.params);
}

pid_t
SpawnChildProcess(PreparedChildProcess &&params)
{
    int clone_flags = SIGCHLD;
    clone_flags = params.ns.GetCloneFlags(clone_flags);

    SpawnChildProcessContext ctx(params);

    /* avoid race condition due to libevent signal handler in child
       process */
    enter_signal_section(&ctx.signals);

    char stack[8192];
    long pid = clone(spawn_fn, stack + sizeof(stack), clone_flags, &ctx);
    if (pid < 0) {
        int e = errno;
        leave_signal_section(&ctx.signals);
        return -e;
    }

    leave_signal_section(&ctx.signals);
    return pid;
}
