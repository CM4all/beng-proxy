// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SocketPairIstream.hxx"
#include "New.hxx"
#include "UnusedPtr.hxx"
#include "ForwardIstream.hxx"
#include "Bucket.hxx"
#include "event/SocketEvent.hxx"
#include "net/SocketError.hxx"
#include "io/Splice.hxx"
#include "io/SpliceSupport.hxx"
#include "util/Compiler.h"

#include <cassert>

#include <errno.h>
#include <sys/socket.h>

class SocketPairIstream final : public ForwardIstream {
	std::size_t in_socket = 0;

	SocketEvent r, w;

public:
	SocketPairIstream(struct pool &p, EventLoop &event_loop,
			  UnusedIstreamPtr _input) noexcept
		:ForwardIstream(p, std::move(_input)),
		 r(event_loop, BIND_THIS_METHOD(OnRead)),
		 w(event_loop, BIND_THIS_METHOD(OnWrite)) {
		input.SetDirect(ISTREAM_TO_SOCKET);
	}

	~SocketPairIstream() noexcept override {
		r.Close();
		w.Close();
	}

	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override;
	off_t _GetAvailable(bool partial) noexcept override;
	void _Read() noexcept override;

	void _FillBucketList(IstreamBucketList &list) override {
		/* refuse to use buckets */
		list.EnableFallback();
	}

	void _ConsumeDirect(std::size_t nbytes) noexcept override;

	/* handler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;

private:
	bool CreateSocketPair() noexcept;

	void OnRead(unsigned events) noexcept;
	void OnWrite(unsigned events) noexcept;

	IstreamDirectResult Consume() noexcept;
};

IstreamDirectResult
SocketPairIstream::Consume() noexcept
{
	assert(r.IsDefined());
	assert(in_socket > 0);

	auto result = InvokeDirect(FdType::FD_SOCKET, r.GetSocket().ToFileDescriptor(),
				   IstreamHandler::NO_OFFSET, in_socket,
				   !HasInput());
	switch (result) {
	case IstreamDirectResult::BLOCKING:
	case IstreamDirectResult::CLOSED:
		/* handler blocks or socket was closed */
		break;

	case IstreamDirectResult::END:
		/* must not happen */
		assert(false);
		gcc_unreachable();

	case IstreamDirectResult::ERRNO:
		if (errno != EAGAIN) {
			DestroyError(std::make_exception_ptr(MakeSocketError("read from socket failed")));
			result = IstreamDirectResult::CLOSED;
		}

		break;

	case IstreamDirectResult::OK:
		if (in_socket == 0) {
			if (input.IsDefined()) {
				r.CancelRead();
			} else if (!input.IsDefined()) {
				/* our input has already reported EOF,
				   and we have been waiting for the
				   socket to become empty */
				DestroyEof();
				result = IstreamDirectResult::CLOSED;
			}
		} else
			r.ScheduleRead();

		break;
	}

	return result;
}

bool
SocketPairIstream::CreateSocketPair() noexcept
{
	assert(!r.IsDefined());
	assert(!w.IsDefined());

	SocketDescriptor _r, _w;

	if (!SocketDescriptor::CreateSocketPairNonBlock(AF_LOCAL, SOCK_STREAM, 0,
							_r, _w)) {
		DestroyError(std::make_exception_ptr(MakeSocketError("Failed to create socket pair")));
		return false;
	}

	r.Open(_r);
	r.ScheduleRead();
	w.Open(_w);
	return true;
}

void
SocketPairIstream::OnRead(unsigned) noexcept
{
	assert(in_socket > 0);

	Consume();
}

void
SocketPairIstream::OnWrite(unsigned) noexcept
{
	input.Read();
}


/*
 * istream handler
 *
 */

std::size_t
SocketPairIstream::OnData(std::span<const std::byte> src) noexcept
{
	assert(HasHandler());

	if (!w.IsDefined() && !CreateSocketPair())
		return 0;

	auto nbytes = w.GetSocket().Write(src.data(), src.size());
	if (nbytes <= 0) [[unlikely]] {
		if (nbytes == 0)
			DestroyError(std::make_exception_ptr(std::runtime_error{"Empty send"}));
		else if (errno == EAGAIN)
			w.ScheduleWrite();
		else
			DestroyError(std::make_exception_ptr(MakeSocketError("Send failed")));

		return 0;
	}

	in_socket += nbytes;
	w.ScheduleRead();
	w.ScheduleWrite();

	return nbytes;
}

inline IstreamDirectResult
SocketPairIstream::OnDirect([[maybe_unused]] FdType type, FileDescriptor fd,
			    off_t offset, std::size_t max_length, bool then_eof) noexcept
{
	assert(HasHandler());

	if (!w.IsDefined() && !CreateSocketPair())
		return IstreamDirectResult::CLOSED;

	ssize_t nbytes = SpliceToSocket(type, fd, ToOffsetPointer(offset),
					w.GetSocket().ToFileDescriptor(),
					max_length);
	if (nbytes <= 0) [[unlikely]] {
		if (nbytes == 0)
			return IstreamDirectResult::END;

		if (errno != EAGAIN)
			return IstreamDirectResult::ERRNO;

		if (!w.GetSocket().IsReadyForWriting()) {
			w.ScheduleWrite();
			return IstreamDirectResult::BLOCKING;
		}

		nbytes = SpliceToSocket(type, fd, ToOffsetPointer(offset),
					w.GetSocket().ToFileDescriptor(),
					max_length);
		if (nbytes <= 0)
			return nbytes < 0
				? IstreamDirectResult::ERRNO
				: IstreamDirectResult::END;
	}

	input.ConsumeDirect(nbytes);
	in_socket += nbytes;

	IstreamDirectResult result = IstreamDirectResult::OK;
	if (then_eof && static_cast<std::size_t>(nbytes) == max_length) {
		w.Close();
		CloseInput();
		result = IstreamDirectResult::CLOSED;
	}

	r.ScheduleRead();

	return result;
}

void
SocketPairIstream::OnEof() noexcept
{
	input.Clear();
	w.Close();

	if (in_socket == 0)
		DestroyEof();
}

inline void
SocketPairIstream::OnError(std::exception_ptr ep) noexcept
{
	input.Clear();
	DestroyError(ep);
}

/*
 * istream implementation
 *
 */

void
SocketPairIstream::_SetDirect([[maybe_unused]] FdTypeMask mask) noexcept
{
	// always enabled
}

off_t
SocketPairIstream::_GetAvailable(bool partial) noexcept
{
	if (input.IsDefined()) [[likely]] {
		off_t available = input.GetAvailable(partial);
		if (in_socket > 0) {
			if (available != -1)
				available += in_socket;
			else if (partial)
				available = in_socket;
		}

		return available;
	} else {
		assert(in_socket > 0);

		return in_socket;
	}
}

void
SocketPairIstream::_Read() noexcept
{
	if (in_socket > 0 && (Consume() != IstreamDirectResult::OK || in_socket > 0))
		return;

	/* at this point, the pipe must be flushed - if the pipe is
	   flushed, this stream is either closed or there must be an input
	   stream */
	assert(input.IsDefined());

	input.Read();
}

void
SocketPairIstream::_ConsumeDirect(std::size_t nbytes) noexcept
{
	assert(in_socket >= nbytes);

	in_socket -= nbytes;
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
NewSocketPairIstream(struct pool &pool, EventLoop &event_loop,
		     UnusedIstreamPtr input) noexcept
{
	return NewIstreamPtr<SocketPairIstream>(pool, event_loop, std::move(input));
}
