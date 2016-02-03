/*
 * Launch "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_launch.hxx"
#include "lhttp_address.hxx"
#include "spawn/Spawn.hxx"
#include "spawn/Prepared.hxx"

#include <daemon/log.h>
#include <inline/compiler.h>

#include <glib.h>

#include <unistd.h>

gcc_noreturn
void
lhttp_run(const LhttpAddress *address, int fd)
{
    PreparedChildProcess e;
    e.stdin_fd = fd;

    e.Append(address->path);

    for (unsigned i = 0; i < address->args.n; ++i)
        e.Append(address->args.values[i]);

    GError *error = nullptr;
    if (!address->options.CopyTo(e, nullptr, &error)) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        _exit(EXIT_FAILURE);
    }

    Exec(std::move(e));
}
