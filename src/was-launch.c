/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was-launch.h"
#include "fd_util.h"
#include "fd-util.h"
#include "exec.h"
#include "jail.h"
#include "sigutil.h"

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
was_run(const char *executable_path,
        const struct jail_params *jail,
        int control_fd, int input_fd, int output_fd)
{
    dup2(input_fd, 0);
    dup2(output_fd, 1);
    /* fd2 is retained */
    dup2(control_fd, 3);

    struct exec e;
    exec_init(&e);
    jail_wrapper_insert(&e, jail, NULL);
    exec_append(&e, executable_path);
    exec_do(&e);

    fprintf(stderr, "failed to execute %s: %s\n",
            executable_path, strerror(errno));
    _exit(1);
}

bool
was_launch(struct was_process *process,
           const char *executable_path,
           const struct jail_params *jail,
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

    /* avoid race condition due to libevent signal handler in child
       process */
    sigset_t signals;
    enter_signal_section(&signals);

    pid_t pid = fork();
    if (pid < 0) {
        leave_signal_section(&signals);

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

    if (pid == 0) {
        install_default_signal_handlers();
        leave_signal_section(&signals);

        was_run(executable_path, jail,
                control_fds[1], output_fds[0], input_fds[1]);
    }

    leave_signal_section(&signals);

    close(control_fds[1]);
    close(input_fds[1]);
    close(output_fds[0]);

    fd_set_nonblock(input_fds[0], true);
    fd_set_nonblock(output_fds[1], true);

    process->pid = pid;
    process->control_fd = control_fds[0];
    process->input_fd = input_fds[0];
    process->output_fd = output_fds[1];
    return true;
}
