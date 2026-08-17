// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Launch.hxx"
#include "pool/tpool.hxx"
#include "cgi/ChildParams.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Mount.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "net/ListenStreamStock.hxx"
#include "net/SocketDescriptor.hxx"
#include "io/FdHolder.hxx"
#include "AllocatorPtr.hxx"

#include <fmt/format.h>

static auto
WasLaunch(SpawnService &spawn_service,
	  ListenStreamStock *listen_stream_stock,
	  SharedLease &listen_stream_lease,
	  std::string_view name,
	  const CgiChildParams &params,
	  UniqueFileDescriptor &&stderr_fd,
	  WasSocket &&socket)
{
	PreparedChildProcess p;
	p.control_fd = socket.control.ToFileDescriptor();
	p.stdout_fd = socket.output;
	p.stdin_fd = socket.input;

	const TempPoolLease tpool;
	if (p.ns.mount.mount_listen_stream.data() != nullptr) {
		if (listen_stream_stock == nullptr)
			throw std::runtime_error{"No ListenStreamStock"};

		const AllocatorPtr alloc{tpool};

		/* copy the mount list before editing it, which is
		   currently a shallow copy pointing to inside the
		   translation cache*/
		p.ns.mount.mounts = Mount::CloneAll(alloc, p.ns.mount.mounts);

		listen_stream_lease = listen_stream_stock->Apply(alloc, p.ns.mount);
	}

	FdHolder close_fds;
	params.CopyTo(p, close_fds);

	if (!p.stderr_fd.IsDefined())
		p.stderr_fd = stderr_fd;

#ifdef HAVE_LIBSYSTEMD
	if (p.sigkill && p.cgroup != nullptr && p.cgroup->name != nullptr && p.cgroup_session == nullptr) {
		// TODO use a better session cgroup name
		static unsigned session_id_counter = 0;
		p.strings.emplace_front(fmt::format("session-{}", ++session_id_counter));
		p.cgroup_session = p.strings.front().c_str();
	}
#endif // HAVE_LIBSYSTEMD

	return spawn_service.SpawnChildProcess(name, std::move(p));
}

WasProcess
was_launch(SpawnService &spawn_service,
	   ListenStreamStock *listen_stream_stock,
	   std::string_view name,
	   const CgiChildParams &params,
	   UniqueFileDescriptor stderr_fd)
{
	auto s = WasSocket::CreatePair();

	WasProcess process(std::move(s.first));
	process.input.SetNonBlocking();
	process.output.SetNonBlocking();

	process.handle = WasLaunch(spawn_service, listen_stream_stock,
				   process.listen_stream_lease,
				   name, params,
				   std::move(stderr_fd),
				   std::move(s.second));
	return process;
}
