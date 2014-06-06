/*
 * Launch "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_launch.hxx"
#include "lhttp_address.hxx"
#include "exec.hxx"

#include <daemon/log.h>
#include <inline/compiler.h>

#include <unistd.h>
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

    for (auto i : address->env)
        putenv(const_cast<char *>(i));

    Exec e;
    jail_wrapper_insert(e, &address->options.jail, nullptr);
    e.Append(address->path);

    for (unsigned i = 0; i < address->args.n; ++i)
        e.Append(address->args.values[i]);

    e.DoExec();
}
