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
        dup2(fd, 2);
    } else {
        close(1);
        close(2);
    }

    Exec e;

    for (auto i : env)
        e.PutEnv(i);

    jail_wrapper_insert(e, jail, nullptr);
    e.Append(executable_path);
    for (auto i : args)
        e.Append(i);
    e.DoExec();
}
