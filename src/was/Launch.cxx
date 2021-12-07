/*
 * Copyright 2007-2021 CM4all GmbH
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
#include "net/SocketDescriptor.hxx"
#include "util/ConstBuffer.hxx"

static auto
WasLaunch(SpawnService &spawn_service,
	  const char *name,
	  const char *executable_path,
	  ConstBuffer<const char *> args,
	  const ChildOptions &options,
	  UniqueFileDescriptor &&stderr_fd,
	  WasSocket &&socket)
{
	PreparedChildProcess p;
	p.SetControl(std::move(socket.control));
	p.SetStdout(std::move(socket.output));
	p.SetStdin(std::move(socket.input));

	p.Append(executable_path);
	for (auto i : args)
		p.Append(i);

	options.CopyTo(p);

	if (!p.stderr_fd.IsDefined() && stderr_fd.IsDefined())
		p.SetStderr(std::move(stderr_fd));

	return spawn_service.SpawnChildProcess(name, std::move(p));
}

WasProcess
was_launch(SpawnService &spawn_service,
	   const char *name,
	   const char *executable_path,
	   ConstBuffer<const char *> args,
	   const ChildOptions &options,
	   UniqueFileDescriptor stderr_fd)
{
	auto s = WasSocket::CreatePair();

	WasProcess process(std::move(s.first));
	process.input.SetNonBlocking();
	process.output.SetNonBlocking();

	process.handle = WasLaunch(spawn_service, name, executable_path, args,
				   options, std::move(stderr_fd),
				   std::move(s.second));
	return process;
}
