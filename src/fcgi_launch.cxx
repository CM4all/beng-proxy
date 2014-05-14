/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi_launch.hxx"
#include "exec.h"
#include "jail.h"

#include <daemon/log.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

gcc_noreturn
void
fcgi_run(const struct jail_params *jail,
         const char *executable_path,
         const char *const*args, unsigned n_args)

{
    int fd = open("/dev/null", O_WRONLY);
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
    jail_wrapper_insert(&e, jail, nullptr);
    exec_append(&e, executable_path);
    for (unsigned i = 0; i < n_args; ++i)
        exec_append(&e, args[i]);
    exec_do(&e);

    daemon_log(1, "failed to execute %s: %s\n",
               executable_path, strerror(errno));
    _exit(1);
}
