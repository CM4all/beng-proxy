// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ChildStockItem.hxx"
#include "ChildStock.hxx"
#include "ListenStreamSpawnStock.hxx"
#include "pool/tpool.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Mount.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ProcessHandle.hxx"
#include "net/EasyMessage.hxx"
#include "net/SocketError.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/FdHolder.hxx"
#include "util/StringList.hxx"
#include "AllocatorPtr.hxx"

#include <cassert>

#include <sys/socket.h>
#include <unistd.h>

ChildStockItem::ChildStockItem(CreateStockItem c,
			       ChildStock &_child_stock,
			       std::string_view _tag) noexcept
	:StockItem(c),
	 child_stock(_child_stock),
	 tag(_tag) {}

ChildStockItem::~ChildStockItem() noexcept = default;

void
ChildStockItem::Prepare(ChildStockClass &cls, void *info,
			PreparedChildProcess &p,
			FdHolder &close_fds)
{
	cls.PrepareChild(info, p, close_fds);
}

void
ChildStockItem::Spawn(ChildStockClass &cls, void *info,
		      SocketDescriptor log_socket,
		      const ChildErrorLogOptions &log_options)
{
	FdHolder close_fds;
	PreparedChildProcess p;
	Prepare(cls, info, p, close_fds);

	if (p.ns.mount.mount_listen_stream.data() != nullptr) {
		const TempPoolLease tpool;
		const AllocatorPtr alloc{tpool};

		/* copy the mount list before editing it, which is
		   currently a shallow copy pointing to inside the
		   translation cache*/
		p.ns.mount.mounts = Mount::CloneAll(alloc, p.ns.mount.mounts);

		if (auto *listen_stream_spawn_stock = child_stock.GetListenStreamSpawnStock()) {
			listen_stream_lease = listen_stream_spawn_stock->Apply(alloc,
									       p.ns.mount);
		} else
			throw std::runtime_error{"No ListenStreamSpawnStock"};
	}

	if (log_socket.IsDefined() && !p.stderr_fd.IsDefined() &&
	    p.stderr_path == nullptr)
		log.EnableClient(p, close_fds,
				 GetEventLoop(), log_socket, log_options,
				 cls.WantStderrPond(info));

	UniqueSocketDescriptor stderr_socket1;
	if (p.stderr_path != nullptr &&
	    cls.WantStderrFd(info) &&
	    !UniqueSocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_SEQPACKET, 0,
						      stderr_socket1, p.return_stderr))
		throw MakeSocketError("socketpair() failed");

	if (p.stderr_fd.IsDefined() && cls.WantStderrFd(info))
		stderr_fd = p.stderr_fd.Duplicate();

	auto &spawn_service = child_stock.GetSpawnService();
	handle = spawn_service.SpawnChildProcess(GetStockName(), std::move(p));
	handle->SetExitListener(*this);

	if (stderr_socket1.IsDefined()) {
		if (p.return_stderr.IsDefined())
			p.return_stderr.Close();

		stderr_fd = EasyReceiveMessageWithOneFD(stderr_socket1);
	}
}

bool
ChildStockItem::IsTag(std::string_view _tag) const noexcept
{
	return StringListContains(tag, '\0', _tag);
}

UniqueFileDescriptor
ChildStockItem::GetStderr() const noexcept
{
	return stderr_fd.IsDefined()
		? stderr_fd.Duplicate()
		: UniqueFileDescriptor{};
}

bool
ChildStockItem::Borrow() noexcept
{
	assert(!busy);
	busy = true;

	/* remove from ChildStock::idle list */
	assert(AutoUnlinkIntrusiveListHook::is_linked());
	AutoUnlinkIntrusiveListHook::unlink();

	return true;
}

bool
ChildStockItem::Release() noexcept
{
	assert(busy);
	busy = false;

	/* reuse this item only if the child process hasn't exited */
	if (!handle)
		return false;

	assert(!AutoUnlinkIntrusiveListHook::is_linked());
	child_stock.AddIdle(*this);

	return true;
}

void
ChildStockItem::OnChildProcessExit(int) noexcept
{
	assert(handle);
	handle.reset();

	if (busy)
		InvokeBusyDisconnect();
	else
		InvokeIdleDisconnect();
}

void
ChildStockItem::Disconnected() noexcept
{
	Fade();

	if (busy)
		InvokeBusyDisconnect();
	else
		InvokeIdleDisconnect();
}
