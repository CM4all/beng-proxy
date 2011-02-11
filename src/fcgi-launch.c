/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-launch.h"

#include <daemon/log.h>
#include <inline/compiler.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdbool.h>

pid_t
fcgi_spawn_child(const char *executable_path, const char *jail_path, int fd)
{
    pid_t pid = fork();
    if (pid < 0) {
        daemon_log(2, "fork() failed: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0) {
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
        daemon_log(1, "failed to execute %s: %s\n",
                   executable_path, strerror(errno));
        _exit(1);
    }

    return pid;
}
