/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-launch.h"
#include "exec.h"
#include "jail.h"
#include "sigutil.h"
#include "gerrno.h"

#include <daemon/log.h>
#include <inline/compiler.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdbool.h>

gcc_noreturn
static void
fcgi_run(const struct jail_params *jail,
         const char *executable_path,
         int fd)
{
    dup2(fd, 0);
    close(fd);

    fd = open("/dev/null", O_WRONLY);
    if (fd >= 0) {
        dup2(fd, 1);
        dup2(fd, 2);
    } else {
        close(1);
        close(2);
    }

    clearenv();

    struct exec e;
    exec_init(&e);
    jail_wrapper_insert(&e, jail, NULL);
    exec_append(&e, executable_path);
    exec_do(&e);

    daemon_log(1, "failed to execute %s: %s\n",
               executable_path, strerror(errno));
    _exit(1);
}

pid_t
fcgi_spawn_child(const struct jail_params *jail,
                 const char *executable_path, int fd,
                 GError **error_r)
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

        fcgi_run(jail,
                 executable_path,
                 fd);
    }

    leave_signal_section(&signals);

    return pid;
}
