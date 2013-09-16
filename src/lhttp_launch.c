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
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>

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

static void
lhttp_socket_path(struct sockaddr_un *address)
{
    address->sun_family = AF_UNIX;

    strcpy(address->sun_path, "/tmp/cm4all-beng-proxy-lhttp-XXXXXX");
    mktemp(address->sun_path);
}

static int
lhttp_create_socket(struct lhttp_process *process, GError **error_r)
{
    lhttp_socket_path(&process->address);

    int ret = unlink(process->address.sun_path);
    if (ret != 0 && errno != ENOENT) {
        set_error_errno_msg(error_r, "Failed to unlink socket");
        return -1;
    }

    int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        set_error_errno_msg(error_r, "Failed to create socket");
        return -1;
    }

    ret = bind(fd, (const struct sockaddr*)&process->address,
               SUN_LEN(&process->address));
    if (ret < 0) {
        set_error_errno_msg(error_r, "Bind failed");
        close(fd);
        return -1;
    }

    // TODO: fix race condition
    chmod(process->address.sun_path, 0700);

    ret = listen(fd, 8);
    if (ret < 0) {
        set_error_errno_msg(error_r, "listen() failed");
        close(fd);
        return -1;
    }

    return fd;
}

bool
lhttp_launch(struct lhttp_process *process,
             const struct lhttp_address *address,
             GError **error_r)
{
    int fd = lhttp_create_socket(process, error_r);
    if (fd < 0)
        return false;

    process->pid = lhttp_start(address, fd, error_r);
    close(fd);
    return process->pid > 0;
}
