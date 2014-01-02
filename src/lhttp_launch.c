/*
 * Launch "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_launch.h"
#include "lhttp_address.h"
#include "exec.h"

#include <daemon/log.h>
#include <inline/compiler.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

gcc_noreturn
void
lhttp_run(const struct lhttp_address *address, int fd)
{
    if (fd != 0) {
        dup2(fd, 0);
        close(fd);
    }

    clearenv();

    struct exec e;
    exec_init(&e);
    jail_wrapper_insert(&e, &address->options.jail, NULL);
    exec_append(&e, address->path);

    for (unsigned i = 0; i < address->num_args; ++i)
        exec_append(&e, address->args[i]);

    exec_do(&e);

    daemon_log(1, "failed to execute %s: %s\n",
               address->path, strerror(errno));
    _exit(1);
}
