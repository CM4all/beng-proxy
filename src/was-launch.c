/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was-launch.h"
#include "fd_util.h"
#include "socket-util.h"

#include <daemon/log.h>
#include <inline/compiler.h>

#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

__attr_noreturn
static void
was_run(const char *executable_path, const char *jail_path,
        int control_fd, int input_fd, int output_fd)
{
    dup2(input_fd, 0);
    dup2(output_fd, 1);
    /* fd2 is retained */
    dup2(control_fd, 3);

    if (jail_path != NULL)
        execl("/usr/lib/cm4all/jailcgi/bin/wrapper", "wrapper",
              "-d", jail_path,
              executable_path, NULL);
    else
        execl(executable_path, executable_path, NULL);

    fprintf(stderr, "failed to execute %s: %s\n",
            executable_path, strerror(errno));
    _exit(1);
}

bool
was_launch(struct was_process *process,
           const char *executable_path, const char *jail_path,
           GError **error_r)
{
    int control_fds[2], input_fds[2], output_fds[2];

    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, control_fds) < 0) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "failed to create socket pair: %s", strerror(errno));
        return false;
    }

    if (pipe_cloexec(input_fds) < 0) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "failed to create first pipe: %s", strerror(errno));
        close(control_fds[0]);
        close(control_fds[1]);
        return false;
    }

    if (pipe_cloexec(output_fds) < 0) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "failed to create second pipe: %s", strerror(errno));
        close(control_fds[0]);
        close(control_fds[1]);
        close(input_fds[0]);
        close(input_fds[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "fork failed: %s", strerror(errno));
        close(control_fds[0]);
        close(control_fds[1]);
        close(input_fds[0]);
        close(input_fds[1]);
        close(output_fds[0]);
        close(output_fds[1]);
        return false;
    }

    if (pid == 0)
        was_run(executable_path, jail_path,
                control_fds[1], output_fds[0], input_fds[1]);

    close(control_fds[1]);
    close(input_fds[1]);
    close(output_fds[0]);

    socket_set_nonblock(input_fds[0], true);
    socket_set_nonblock(output_fds[1], true);

    process->pid = pid;
    process->control_fd = control_fds[0];
    process->input_fd = input_fds[0];
    process->output_fd = output_fds[1];
    return true;
}
