// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Launch.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "net/SocketDescriptor.hxx"

static auto
WasLaunch(SpawnService &spawn_service,
	  const char *name,
	  const char *executable_path,
	  std::span<const char *const> args,
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
	   std::span<const char *const> args,
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
