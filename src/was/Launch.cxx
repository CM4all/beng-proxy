/*
 * Copyright 2007-2020 CM4all GmbH
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
	   UniqueFileDescriptor stderr_fd,
	   ExitListener *listener)
{
	auto s = WasSocket::CreatePair();

	WasProcess process(std::move(s.first));
	process.input.SetNonBlocking();
	process.output.SetNonBlocking();

	/* allocate 256 kB for each pipe to reduce the system call and
	   latency overhead for splicing */
	static constexpr int PIPE_BUFFER_SIZE = 256 * 1024;
	fcntl(process.input.Get(), F_SETPIPE_SZ, PIPE_BUFFER_SIZE);
	fcntl(process.output.Get(), F_SETPIPE_SZ, PIPE_BUFFER_SIZE);

	PreparedChildProcess p;
	p.SetControl(std::move(s.second.control));
	p.SetStdout(std::move(s.second.output));
	p.SetStdin(std::move(s.second.input));

	p.Append(executable_path);
	for (auto i : args)
		p.Append(i);

	options.CopyTo(p, true, nullptr);

	if (p.stderr_fd < 0 && stderr_fd.IsDefined())
		p.SetStderr(std::move(stderr_fd));

	process.pid = spawn_service.SpawnChildProcess(name, std::move(p),
						      listener);
	return process;
}
