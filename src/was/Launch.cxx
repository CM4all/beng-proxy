// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Launch.hxx"
#include "pool/tpool.hxx"
#include "spawn/Interface.hxx"
#include "spawn/ListenStreamSpawnStock.hxx"
#include "spawn/Mount.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "net/SocketDescriptor.hxx"
#include "io/FdHolder.hxx"
#include "AllocatorPtr.hxx"

static auto
WasLaunch(SpawnService &spawn_service,
	  ListenStreamSpawnStock *listen_stream_spawn_stock,
	  SharedLease &listen_stream_lease,
	  const char *name,
	  const char *executable_path,
	  std::span<const char *const> args,
	  const ChildOptions &options,
	  UniqueFileDescriptor &&stderr_fd,
	  WasSocket &&socket)
{
	PreparedChildProcess p;
	p.control_fd = socket.control.ToFileDescriptor();
	p.stdout_fd = socket.output;
	p.stdin_fd = socket.input;

	const TempPoolLease tpool;
	if (p.ns.mount.mount_listen_stream.data() != nullptr) {
		if (listen_stream_spawn_stock == nullptr)
			throw std::runtime_error{"No ListenStreamSpawnStock"};

		const AllocatorPtr alloc{tpool};

		/* copy the mount list before editing it, which is
		   currently a shallow copy pointing to inside the
		   translation cache*/
		p.ns.mount.mounts = Mount::CloneAll(alloc, p.ns.mount.mounts);

		listen_stream_lease = listen_stream_spawn_stock->Apply(alloc,
								       p.ns.mount);
	}

	p.Append(executable_path);
	for (auto i : args)
		p.Append(i);

	FdHolder close_fds;
	options.CopyTo(p, close_fds);

	if (!p.stderr_fd.IsDefined())
		p.stderr_fd = stderr_fd;

	return spawn_service.SpawnChildProcess(name, std::move(p));
}

WasProcess
was_launch(SpawnService &spawn_service,
	   ListenStreamSpawnStock *listen_stream_spawn_stock,
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

	process.handle = WasLaunch(spawn_service, listen_stream_spawn_stock,
				   process.listen_stream_lease,
				   name, executable_path, args,
				   options, std::move(stderr_fd),
				   std::move(s.second));
	return process;
}
