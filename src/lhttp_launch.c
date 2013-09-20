/*
 * Launch "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_launch.h"
#include "lhttp_address.h"
#include "exec.h"
#include "sigutil.h"
#include "gerrno.h"

#include <daemon/log.h>
#include <inline/compiler.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

gcc_noreturn
static void
lhttp_run(const struct lhttp_address *address, int fd)
{
    if (fd != 0) {
        dup2(fd, 0);
        close(fd);
    }

    clearenv();

    struct exec e;
    exec_init(&e);
    jail_wrapper_insert(&e, &address->jail, NULL);
    exec_append(&e, address->path);

    for (unsigned i = 0; i < address->num_args; ++i)
        exec_append(&e, address->args[i]);

    exec_do(&e);

    daemon_log(1, "failed to execute %s: %s\n",
               address->path, strerror(errno));
    _exit(1);
}

static pid_t
lhttp_start(const struct lhttp_address *address, int fd, GError **error_r)
{
    /* avoid race condition due to libevent signal handler in child
       process */
    sigset_t signals;
    enter_signal_section(&signals);

    pid_t pid = fork();
    if (pid < 0) {
        set_error_errno_msg(error_r, "fork() failed");
        leave_signal_section(&signals);
        return -1;
    }

    if (pid == 0) {
        install_default_signal_handlers();
        leave_signal_section(&signals);

        lhttp_run(address, fd);
    }

    leave_signal_section(&signals);

    return pid;
}

bool
lhttp_launch(struct lhttp_process *process,
             const struct lhttp_address *address,
             GError **error_r)
{
    int fd = child_socket_create(&process->socket, error_r);
    if (fd < 0)
        return false;

    process->pid = lhttp_start(address, fd, error_r);
    close(fd);
    if (process->pid <= 0) {
        child_socket_unlink(&process->socket);
        return false;
    }

    return true;
}
