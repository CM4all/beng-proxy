/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Launch.hxx"
#include "spawn/Spawn.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "util/ConstBuffer.hxx"

#include <glib.h>

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

gcc_noreturn
void
fcgi_run(const ChildOptions &options,
         const char *executable_path,
         ConstBuffer<const char *> args)
{
    /* the FastCGI protocol defines a channel for stderr, so we could
       close its "real" stderr here, but many FastCGI applications
       don't use the FastCGI protocol to send error messages, so we
       just keep it open */

    PreparedChildProcess e;

    int fd = open("/dev/null", O_WRONLY|O_CLOEXEC|O_NOCTTY);
    if (fd >= 0)
        e.stdout_fd = fd;

    e.Append(executable_path);
    for (auto i : args)
        e.Append(i);

    GError *error = nullptr;
    if (!options.CopyTo(e, true, nullptr, &error)) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        _exit(EXIT_FAILURE);
    }

    Exec(std::move(e));
}
