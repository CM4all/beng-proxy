/*
 * Copyright 2007-2019 CM4all GmbH
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
SendToBuffer(FilteredSocket &socket, const void *data, size_t length) noexcept
{
	const auto nbytes = socket.Write(data, length);
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
