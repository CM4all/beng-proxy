/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-launch.h"
#include "exec.h"
#include "jail.h"

#include <daemon/log.h>
#include <inline/compiler.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdbool.h>

__attr_noreturn
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
    pid_t pid = fork();
    if (pid < 0) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "fork() failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0)
        fcgi_run(jail,
                 executable_path,
                 fd);

    return pid;
}
