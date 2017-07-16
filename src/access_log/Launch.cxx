/*
 * Launch logger child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Launch.hxx"
#include "spawn/UidGid.hxx"
#include "system/Error.hxx"
#include "util/PrintException.hxx"

#include "util/Compiler.h"

#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

gcc_noreturn
static void
log_run(const char *program, UniqueSocketDescriptor &&fd)
{
    fd.CheckDuplicate(FileDescriptor(STDIN_FILENO));

    execl("/bin/sh", "sh", "-c", program, nullptr);
    fprintf(stderr, "failed to execute %s: %s\n",
            program, strerror(errno));
    _exit(1);
}

LogProcess
log_launch(const char *program,
           const UidGid *user)
{
    LogProcess p;
    UniqueSocketDescriptor server_fd;

    if (!UniqueSocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_SEQPACKET, 0,
                                                  server_fd, p.fd))
        throw MakeErrno("socketpair() failed");

    /* we need an unidirectional socket only */
    shutdown(p.fd.Get(), SHUT_RD);
    shutdown(server_fd.Get(), SHUT_WR);

    p.pid = fork();
    if (p.pid < 0)
        throw MakeErrno("fork() failed");

    if (p.pid == 0) {
        try {
            if (user != nullptr)
                user->Apply();

            log_run(program, std::move(server_fd));
        } catch (...) {
            PrintException(std::current_exception());
            _exit(EXIT_FAILURE);
        }
    }

    return p;
}
