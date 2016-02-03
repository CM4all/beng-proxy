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

    GError *error = nullptr;
    if (!address->CopyTo(e, &error)) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        _exit(EXIT_FAILURE);
    }

    Exec(std::move(e));
}
