/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was-launch.h"
#include "fd_util.h"

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

    if (jail_path != NULL) {
        setenv("DOCUMENT_ROOT", jail_path, true);
        setenv("JAILCGI_ACTION", executable_path, true);
        executable_path = "/usr/lib/cm4all/jailcgi/bin/wrapper";

        /* several fake variables to outsmart the jailcgi
           wrapper */
        setenv("GATEWAY_INTERFACE", "dummy", true);
        setenv("JAILCGI_FILENAME", "/tmp/dummy", true);
    }

    execl(executable_path, executable_path, NULL);
    fprintf(stderr, "failed to execute %s: %s\n",
            executable_path, strerror(errno));
    _exit(1);
}

bool
was_launch(struct was_process *process,
           const char *executable_path, const char *jail_path)
{
    int control_fds[2], input_fds[2], output_fds[2];

    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, control_fds) < 0)
        return false;

    if (pipe_cloexec(input_fds) < 0) {
        close(control_fds[0]);
        close(control_fds[1]);
        return false;
    }

    if (pipe_cloexec(output_fds) < 0) {
        close(control_fds[0]);
        close(control_fds[1]);
        close(input_fds[0]);
        close(input_fds[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        daemon_log(2, "fork() failed: %s\n", strerror(errno));
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

    process->pid = pid;
    process->control_fd = control_fds[0];
    process->input_fd = input_fds[0];
    process->output_fd = output_fds[1];
    return true;
}
