// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Client.hxx"
#include "Handler.hxx"
#include "Protocol.hxx"
#include "pool/LeakDetector.hxx"
#include "event/SocketEvent.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SendMessage.hxx"
#include "net/SocketError.hxx"
#include "net/MsgHdr.hxx"
#include "io/Iovec.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"
#include "util/Cancellable.hxx"
#include "util/SpanCast.hxx"
#include "lease.hxx"
#include "AllocatorPtr.hxx"

#include <stdexcept>

#include <assert.h>
#include <string.h>
#include <sys/socket.h>

struct DelegateClient final : PoolLeakDetector, Cancellable {
	LeasePtr lease_ref;
	const SocketDescriptor s;
	SocketEvent event;

	DelegateHandler &handler;

	DelegateClient(EventLoop &event_loop, SocketDescriptor _s, Lease &lease,
		       AllocatorPtr alloc,
		       DelegateHandler &_handler) noexcept
		:PoolLeakDetector(alloc),
		 lease_ref(lease),
		 s(_s), event(event_loop, BIND_THIS_METHOD(SocketEventCallback), s),
		 handler(_handler)
	{
		event.ScheduleRead();
	}

	void Destroy() noexcept {
		this->~DelegateClient();
	}

	void ReleaseSocket(PutAction action) noexcept {
		assert(s.IsDefined());

		/* the SocketEvent must be canceled before releasing its lease
		   to avoid EBADFD from epoll_ctl() */
		event.Cancel();

		lease_ref.Release(action);
	}

	void DestroyError(std::exception_ptr ep) noexcept {
		ReleaseSocket(PutAction::DESTROY);

		auto &_handler = handler;
		Destroy();
		_handler.OnDelegateError(ep);
	}

	void DestroyError(const char *msg) noexcept {
		DestroyError(std::make_exception_ptr(std::runtime_error(msg)));
	}

	void HandleFd(const struct msghdr &msg, size_t length);
	void HandleErrno(size_t length);
	void HandleMsg(const struct msghdr &msg,
		       DelegateResponseCommand command, size_t length);
	void TryRead();

private:
	void SocketEventCallback(unsigned) noexcept {
		TryRead();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		ReleaseSocket(PutAction::DESTROY);
		Destroy();
	}
};

inline void
DelegateClient::HandleFd(const struct msghdr &msg, size_t length)
{
	if (length != 0) {
		DestroyError("Invalid message length");
		return;
	}

	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg == nullptr) {
		DestroyError("No fd passed");
		return;
	}

	if (cmsg->cmsg_type != SCM_RIGHTS) {
		DestroyError("got control message of unknown type");
		return;
	}

	ReleaseSocket(PutAction::REUSE);

	const void *data = CMSG_DATA(cmsg);
	const int *fd_p = (const int *)data;

	auto &_handler = handler;
	Destroy();
	_handler.OnDelegateSuccess(UniqueFileDescriptor(*fd_p));
}

inline void
DelegateClient::HandleErrno(size_t length)
{
	int e;

	if (length != sizeof(e)) {
		DestroyError("Invalid message length");
		return;
	}

	ssize_t nbytes = s.Receive(std::as_writable_bytes(std::span{&e, 1}));
	std::exception_ptr ep;

	if (nbytes == sizeof(e)) {
		ReleaseSocket(PutAction::REUSE);

		ep = std::make_exception_ptr(MakeErrno(e, "Error from delegate"));
	} else {
		const auto error = GetSocketError();

		ReleaseSocket(PutAction::DESTROY);

		ep = std::make_exception_ptr(MakeSocketError(error, "Failed to receive errno"));
	}

	auto &_handler = handler;
	Destroy();
	_handler.OnDelegateError(ep);
}

inline void
DelegateClient::HandleMsg(const struct msghdr &msg,
			  DelegateResponseCommand command, size_t length)
{
	switch (command) {
	case DelegateResponseCommand::FD:
		HandleFd(msg, length);
		return;

	case DelegateResponseCommand::ERRNO:
		/* i/o error */
		HandleErrno(length);
		return;
	}

	DestroyError("Invalid delegate response");
}

inline void
DelegateClient::TryRead()
{
	DelegateResponseHeader header;
	std::array iov{MakeIovecT(header)};
	int new_fd;
	std::byte ccmsg[CMSG_SPACE(sizeof(new_fd))];
	auto msg = MakeMsgHdr(nullptr, iov, ccmsg);
	ssize_t nbytes;

	nbytes = recvmsg(s.Get(), &msg, MSG_CMSG_CLOEXEC);
	if (nbytes < 0) {
		DestroyError(std::make_exception_ptr(MakeSocketError("recvmsg() failed")));
		return;
	}

	if ((size_t)nbytes != sizeof(header)) {
		DestroyError("short recvmsg()");
		return;
	}

	HandleMsg(msg, header.command, header.length);
}

/*
 * constructor
 *
 */

static void
SendDelegatePacket(SocketDescriptor s, DelegateRequestCommand cmd,
		   std::span<const std::byte> payload)
{
	const DelegateRequestHeader header{uint16_t(payload.size()), cmd};

	const struct iovec v[] = {
		MakeIovecT(header),
		MakeIovec(payload),
	};

	auto nbytes = SendMessage(s, MessageHeader{v},
				  MSG_DONTWAIT);
	if (nbytes != sizeof(header) + payload.size())
		throw std::runtime_error("Short send to delegate");
}

void
delegate_open(EventLoop &event_loop, SocketDescriptor s, Lease &lease,
	      AllocatorPtr alloc, const char *path,
	      DelegateHandler &handler,
	      CancellablePointer &cancel_ptr) noexcept
{
	try {
		SendDelegatePacket(s, DelegateRequestCommand::OPEN,
				   AsBytes(path));
	} catch (...) {
		lease.ReleaseLease(PutAction::DESTROY);
		handler.OnDelegateError(std::current_exception());
		return;
	}

	auto d = alloc.New<DelegateClient>(event_loop, s, lease, alloc,
					   handler);
	cancel_ptr = *d;
}
