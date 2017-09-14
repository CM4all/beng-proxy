/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
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
Exec(const char *command)
{
    execl("/bin/sh", "sh", "-c", command, nullptr);
    fprintf(stderr, "failed to execute %s: %s\n",
            command, strerror(errno));
    _exit(EXIT_FAILURE);

}

gcc_noreturn
static void
RunLogger(const char *command, SocketDescriptor fd)
{
    fd.CheckDuplicate(FileDescriptor(STDIN_FILENO));
    Exec(command);
}

LogProcess
LaunchLogger(const char *command,
             const UidGid *user)
{
    LogProcess p;
    UniqueSocketDescriptor server_fd;

    if (!UniqueSocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_SEQPACKET, 0,
                                                  server_fd, p.fd))
        throw MakeErrno("socketpair() failed");

    /* we need an unidirectional socket only */
    p.fd.ShutdownRead();
    server_fd.ShutdownWrite();

    p.pid = fork();
    if (p.pid < 0)
        throw MakeErrno("fork() failed");

    if (p.pid == 0) {
        try {
            if (user != nullptr)
                user->Apply();

            RunLogger(command, server_fd);
        } catch (...) {
            PrintException(std::current_exception());
            _exit(EXIT_FAILURE);
        }
    }

    return p;
}
