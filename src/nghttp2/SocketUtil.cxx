// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SocketUtil.hxx"
#include "Error.hxx"
#include "fs/FilteredSocket.hxx"

#include <nghttp2/nghttp2.h>

namespace NgHttp2 {

BufferedResult
ReceiveFromSocketBuffer(nghttp2_session *session, FilteredSocket &socket)
{
	auto r = socket.ReadBuffer();

	auto nbytes = nghttp2_session_mem_recv(session,
					       (const uint8_t *)r.data(),
					       r.size());
	if (nbytes < 0)
		throw MakeError(int(nbytes), "nghttp2_session_mem_recv() failed");

	socket.DisposeConsumed(nbytes);

	if (nghttp2_session_want_write(session))
		socket.DeferWrite();

	return BufferedResult::MORE; // TODO?
}

ssize_t
SendToBuffer(FilteredSocket &socket, std::span<const std::byte> src) noexcept
{
	const auto nbytes = socket.Write(src);
	if (nbytes < 0) {
		if (nbytes == WRITE_BLOCKING)
			return NGHTTP2_ERR_WOULDBLOCK;
		else
			return NGHTTP2_ERR_CALLBACK_FAILURE;
	}

	return nbytes;
}

bool
OnSocketWrite(nghttp2_session *session, FilteredSocket &socket)
{
	const auto rv = nghttp2_session_send(session);
	if (rv != 0)
		throw MakeError(rv, "nghttp2_session_send() failed");

	if (!nghttp2_session_want_write(session))
		socket.UnscheduleWrite();

	return true;
}

} // namespace NgHttp2
