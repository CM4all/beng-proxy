/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi_launch.hxx"
#include "exec.hxx"
#include "jail.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>

#include <unistd.h>
#include <fcntl.h>

gcc_noreturn
void
fcgi_run(const struct jail_params *jail,
         const char *executable_path,
         ConstBuffer<const char *> args,
         ConstBuffer<const char *> env)

{
    int fd = open("/dev/null", O_WRONLY);
    if (fd >= 0) {
        dup2(fd, 1);
    } else {
        close(1);
    }

    /* the FastCGI protocol defines a channel for stderr, so we could
       close its "real" stderr here, but many FastCGI applications
       don't use the FastCGI protocol to send error messages, so we
       just keep it open */

    Exec e;

    for (auto i : env)
        e.PutEnv(i);

    jail_wrapper_insert(e, jail, nullptr);
    e.Append(executable_path);
    for (auto i : args)
        e.Append(i);
    e.DoExec();
}
