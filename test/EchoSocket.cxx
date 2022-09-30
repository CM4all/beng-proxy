/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "EchoSocket.hxx"
#include "system/Error.hxx"
#include "net/UniqueSocketDescriptor.hxx"

#include <cassert>

EchoSocket::EchoSocket(EventLoop &_event_loop,
		       UniqueSocketDescriptor _fd, FdType _fd_type,
		       SocketFilterPtr _filter)
	:socket(_event_loop)
{
	socket.Init(_fd.Release(), _fd_type,
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
			return BufferedResult::CLOSED;
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
		return BufferedResult::CLOSED;

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
	return socket.Read();
}

void
EchoSocket::OnBufferedError(std::exception_ptr) noexcept
{
	socket.Close();
}
