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

#include "Client.hxx"
#include "Handler.hxx"
#include "Protocol.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "event/SocketEvent.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SendMessage.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"
#include "util/Cancellable.hxx"
#include "lease.hxx"

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
		       struct pool &_pool,
		       DelegateHandler &_handler) noexcept
		:PoolLeakDetector(_pool),
		 lease_ref(lease),
		 s(_s), event(event_loop, BIND_THIS_METHOD(SocketEventCallback), s),
		 handler(_handler)
	{
		event.ScheduleRead();
	}

	void Destroy() noexcept {
		this->~DelegateClient();
	}

	void ReleaseSocket(bool reuse) noexcept {
		assert(s.IsDefined());

		/* the SocketEvent must be canceled before releasing its lease
		   to avoid EBADFD from epoll_ctl() */
		event.Cancel();

		lease_ref.Release(reuse);
	}

	void DestroyError(std::exception_ptr ep) noexcept {
		ReleaseSocket(false);

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
		ReleaseSocket(false);
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

	ReleaseSocket(true);

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

	ssize_t nbytes = recv(s.Get(), &e, sizeof(e), 0);
	std::exception_ptr ep;

	if (nbytes == sizeof(e)) {
		ReleaseSocket(true);

		ep = std::make_exception_ptr(MakeErrno(e, "Error from delegate"));
	} else {
		ReleaseSocket(false);

		ep = std::make_exception_ptr(std::runtime_error("Failed to receive errno"));
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
	struct iovec iov;
	int new_fd;
	char ccmsg[CMSG_SPACE(sizeof(new_fd))];
	struct msghdr msg = {
		.msg_name = nullptr,
		.msg_namelen = 0,
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = ccmsg,
		.msg_controllen = sizeof(ccmsg),
	};
	DelegateResponseHeader header;
	ssize_t nbytes;

	iov.iov_base = &header;
	iov.iov_len = sizeof(header);

	nbytes = recvmsg(s.Get(), &msg, MSG_CMSG_CLOEXEC);
	if (nbytes < 0) {
		DestroyError(std::make_exception_ptr(MakeErrno("recvmsg() failed")));
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
		   const void *payload, size_t length)
{
	const DelegateRequestHeader header{uint16_t(length), cmd};

	struct iovec v[] = {
		{ const_cast<void *>((const void *)&header), sizeof(header) },
		{ const_cast<void *>(payload), length },
	};

	auto nbytes = SendMessage(s,
				  ConstBuffer<struct iovec>(v, std::size(v)),
				  MSG_DONTWAIT);
	if (nbytes != sizeof(header) + length)
		throw std::runtime_error("Short send to delegate");
}

void
delegate_open(EventLoop &event_loop, SocketDescriptor s, Lease &lease,
	      struct pool *pool, const char *path,
	      DelegateHandler &handler,
	      CancellablePointer &cancel_ptr)
{
	try {
		SendDelegatePacket(s, DelegateRequestCommand::OPEN,
				   path, strlen(path));
	} catch (...) {
		lease.ReleaseLease(false);
		handler.OnDelegateError(std::current_exception());
		return;
	}

	auto d = NewFromPool<DelegateClient>(*pool, event_loop, s, lease,
					     *pool,
					     handler);


	cancel_ptr = *d;
}
