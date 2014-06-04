/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi_launch.hxx"
#include "exec.hxx"
#include "jail.hxx"

#include <daemon/log.h>

#include <unistd.h>
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

    Exec e;
    jail_wrapper_insert(e, jail, nullptr);
    e.Append(executable_path);
    for (unsigned i = 0; i < n_args; ++i)
        e.Append(args[i]);
    e.DoExec();
}
