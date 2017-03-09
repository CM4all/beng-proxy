/*
 * Launch logger child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "log_launch.hxx"
#include "system/fd_util.h"
#include "system/Error.hxx"

#include <daemon/user.h>
#include <daemon/log.h>
#include <inline/compiler.h>

#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

gcc_noreturn
static void
log_run(const char *program, int fd)
{
    dup2(fd, 0);

    execl("/bin/sh", "sh", "-c", program, nullptr);
    fprintf(stderr, "failed to execute %s: %s\n",
            program, strerror(errno));
    _exit(1);
}

LogProcess
log_launch(const char *program,
           const struct daemon_user *user)
{
    int fds[2];

    if (socketpair_cloexec(AF_UNIX, SOCK_SEQPACKET, 0, fds) < 0)
        throw MakeErrno("socketpair() failed");

    /* we need an unidirectional socket only */
    shutdown(fds[0], SHUT_RD);
    shutdown(fds[1], SHUT_WR);

    pid_t pid = fork();
    if (pid < 0) {
        int e = errno;
        close(fds[0]);
        close(fds[1]);
        throw MakeErrno(e, "fork() failed");
    }

    if (pid == 0) {
        if (user != nullptr && daemon_user_set(user) < 0)
            _exit(EXIT_FAILURE);

        log_run(program, fds[1]);
    }

    close(fds[1]);

    return {pid, fds[0]};
}
