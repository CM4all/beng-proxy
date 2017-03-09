/*
 * Launch logger child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "log_launch.hxx"
#include "system/Error.hxx"

#include <daemon/user.h>
#include <daemon/log.h>
#include <inline/compiler.h>

#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

gcc_noreturn
static void
log_run(const char *program, UniqueFileDescriptor &&fd)
{
    fd.CheckDuplicate(FileDescriptor(STDIN_FILENO));

    execl("/bin/sh", "sh", "-c", program, nullptr);
    fprintf(stderr, "failed to execute %s: %s\n",
            program, strerror(errno));
    _exit(1);
}

LogProcess
log_launch(const char *program,
           const struct daemon_user *user)
{
    LogProcess p;
    UniqueFileDescriptor server_fd;

    if (!UniqueFileDescriptor::CreateSocketPair(AF_LOCAL, SOCK_SEQPACKET, 0,
                                                server_fd, p.fd))
        throw MakeErrno("socketpair() failed");

    /* we need an unidirectional socket only */
    shutdown(p.fd.Get(), SHUT_RD);
    shutdown(server_fd.Get(), SHUT_WR);

    p.pid = fork();
    if (p.pid < 0)
        throw MakeErrno("fork() failed");

    if (p.pid == 0) {
        if (user != nullptr && daemon_user_set(user) < 0)
            _exit(EXIT_FAILURE);

        log_run(program, std::move(server_fd));
    }

    return p;
}
