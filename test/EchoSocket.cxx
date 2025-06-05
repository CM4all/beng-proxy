// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "EchoSocket.hxx"
#include "system/Error.hxx"
#include "net/UniqueSocketDescriptor.hxx"

#include <cassert>

EchoSocket::EchoSocket(EventLoop &_event_loop,
		       UniqueSocketDescriptor _fd, FdType _fd_type,
		       SocketFilterPtr _filter) noexcept
	:socket(_event_loop)
{
	socket.Init(std::move(_fd), _fd_type,
		    std::chrono::seconds{30},
		    std::move(_filter), *this);
	socket.ScheduleRead();
}

BufferedResult
EchoSocket::OnBufferedData()
{
	auto r = socket.ReadBuffer();
	assert(!r.empty());

	auto nbytes = socket.Write(r);
	if (nbytes >= 0) [[likely]] {
		socket.DisposeConsumed(nbytes);

		if (close_after_data) {
			socket.Close();
			socket.Destroy();
			return BufferedResult::DESTROYED;
		}

		socket.ScheduleWrite();
		return BufferedResult::OK;
	}

	switch (nbytes) {
	case WRITE_ERRNO:
		break;

	case WRITE_BLOCKING:
		return BufferedResult::OK;

	case WRITE_DESTROYED:
		return BufferedResult::DESTROYED;

	case WRITE_BROKEN:
		return BufferedResult::OK;
	}

	throw MakeErrno("Send failed");
}

bool
EchoSocket::OnBufferedClosed() noexcept
{
	socket.Close();
	return false;
}

bool
EchoSocket::OnBufferedWrite()
{
	switch (socket.Read()) {
	case BufferedReadResult::OK:
	case BufferedReadResult::BLOCKING:
		break;

	case BufferedReadResult::DISCONNECTED:
	case BufferedReadResult::DESTROYED:
		return false;
	}

	return true;
}

void
EchoSocket::OnBufferedError(std::exception_ptr) noexcept
{
	socket.Close();
	socket.Destroy();
}
