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
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "system/Error.hxx"
#include "util/ConstBuffer.hxx"

#include "util/Compiler.h"

#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

WasProcess
was_launch(SpawnService &spawn_service,
           const char *name,
           const char *executable_path,
           ConstBuffer<const char *> args,
           const ChildOptions &options,
           ExitListener *listener)
{
    WasProcess process;

    PreparedChildProcess p;

    UniqueSocketDescriptor control_fd;
    if (!UniqueSocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
                                                  control_fd, process.control))
        throw MakeErrno("Failed to create socket pair");

    p.SetControl(std::move(control_fd));

    UniqueFileDescriptor input_r, input_w;
    if (!UniqueFileDescriptor::CreatePipe(input_r, input_w))
        throw MakeErrno("Failed to create first pipe");

    input_r.SetNonBlocking();
    process.input = std::move(input_r);
    p.SetStdout(std::move(input_w));

    UniqueFileDescriptor output_r, output_w;
    if (!UniqueFileDescriptor::CreatePipe(output_r, output_w))
        throw MakeErrno("Failed to create second pipe");

    p.SetStdin(std::move(output_r));
    output_w.SetNonBlocking();
    process.output = std::move(output_w);

    /* allocate 256 kB for each pipe to reduce the system call and
       latency overhead for splicing */
    static constexpr int PIPE_BUFFER_SIZE = 256 * 1024;
    fcntl(process.input.Get(), F_SETPIPE_SZ, PIPE_BUFFER_SIZE);
    fcntl(process.output.Get(), F_SETPIPE_SZ, PIPE_BUFFER_SIZE);

    p.Append(executable_path);
    for (auto i : args)
        p.Append(i);

    options.CopyTo(p, true, nullptr);
    process.pid = spawn_service.SpawnChildProcess(name, std::move(p),
                                                  listener);
    return process;
}
