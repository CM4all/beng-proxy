/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Spawn.hxx"
#include "Prepared.hxx"
#include "system/sigutil.h"
#include "system/fd_util.h"

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
    _exit(1);
}

void
Exec(PreparedChildProcess &&p)
{
    assert(!p.args.empty());

    const char *path = p.Finish();
    Exec(path, p);
}
