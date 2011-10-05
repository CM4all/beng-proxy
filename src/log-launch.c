/*
 * Launch logger child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "log-launch.h"
#include "fd_util.h"

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

    execl("/bin/sh", "sh", "-c", program, NULL);
    fprintf(stderr, "failed to execute %s: %s\n",
            program, strerror(errno));
    _exit(1);
}

bool
log_launch(struct log_process *process, const char *program)
{
    int fds[2];

    if (socketpair_cloexec(AF_UNIX, SOCK_DGRAM, 0, fds) < 0)
        return false;

    /* we need an unidirectional socket only */
    shutdown(fds[0], SHUT_RD);
    shutdown(fds[1], SHUT_WR);

    pid_t pid = fork();
    if (pid < 0) {
        daemon_log(2, "fork() failed: %s\n", strerror(errno));
        close(fds[0]);
        close(fds[1]);
        return false;
    }

    if (pid == 0)
        log_run(program, fds[1]);

    close(fds[1]);

    process->pid = pid;
    process->fd = fds[0];
    return true;
}
