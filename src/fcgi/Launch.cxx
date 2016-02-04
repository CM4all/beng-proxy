/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Launch.hxx"
#include "spawn/Spawn.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/JailParams.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>

#include <unistd.h>
#include <fcntl.h>

gcc_noreturn
void
fcgi_run(const JailParams *jail,
         const char *executable_path,
         ConstBuffer<const char *> args,
         ConstBuffer<const char *> env)

{
    /* the FastCGI protocol defines a channel for stderr, so we could
       close its "real" stderr here, but many FastCGI applications
       don't use the FastCGI protocol to send error messages, so we
       just keep it open */

    PreparedChildProcess e;

    int fd = open("/dev/null", O_WRONLY);
    if (fd >= 0)
        e.stdout_fd = fd;

    for (auto i : env)
        e.PutEnv(i);

    e.Append(executable_path);
    for (auto i : args)
        e.Append(i);
    if (jail != nullptr)
        jail->InsertWrapper(e, nullptr);
    Exec(std::move(e));
}
