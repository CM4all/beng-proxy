/*
 * Launch "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_launch.hxx"
#include "lhttp_address.hxx"
#include "spawn/exec.hxx"

#include <daemon/log.h>
#include <inline/compiler.h>

#include <unistd.h>

gcc_noreturn
void
lhttp_run(const LhttpAddress *address, int fd)
{
    if (fd != 0) {
        dup2(fd, 0);
        close(fd);
    }

    Exec e;

    for (auto i : address->options.env)
        e.PutEnv(i);

    address->options.jail.InsertWrapper(e, nullptr);
    e.Append(address->path);

    for (unsigned i = 0; i < address->args.n; ++i)
        e.Append(address->args.values[i]);

    e.DoExec();
}
