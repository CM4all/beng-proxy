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

#pragma once

#include "Session.hxx"
#include "pool/UniquePtr.hxx"
#include "event/net/BufferedSocket.hxx"
#include "net/SocketAddress.hxx"
#include "util/IntrusiveList.hxx"

struct pool;
class FilteredSocket;
class HttpServerConnectionHandler;
class HttpServerRequestHandler;

namespace NgHttp2 {

class ServerConnection final : BufferedSocketHandler {
	struct pool &pool;

	const UniquePoolPtr<FilteredSocket> socket;

	HttpServerConnectionHandler &handler;
	HttpServerRequestHandler &request_handler;

	const SocketAddress local_address, remote_address;

	const char *const local_host_and_port;
	const char *const remote_host;

	NgHttp2::Session session;

	class Request;
	using RequestList = IntrusiveList<Request>;

	RequestList requests;

#ifndef NDEBUG
	/**
	 * The total number of bytes passed to our
	 * data_chunk_recv_callback() which needs to be reported to
	 * nghttp2_session_consume().
	 */
	size_t unconsumed = 0;
#endif

public:
	ServerConnection(struct pool &_pool,
			 UniquePoolPtr<FilteredSocket> _socket,
			 SocketAddress remote_address,
			 HttpServerConnectionHandler &_handler,
			 HttpServerRequestHandler &request_handler);

	~ServerConnection() noexcept;

	void Consume(std::size_t nbytes) noexcept {
		nghttp2_session_consume_connection(session.get(),
						   nbytes);
		DeferWrite();
	}

private:
	void DeferWrite() noexcept;

	ssize_t SendCallback(std::span<const std::byte> src) noexcept;

	static ssize_t SendCallback(nghttp2_session *, const uint8_t *data,
				    size_t length, int,
				    void *user_data) noexcept {
		auto &c = *(ServerConnection *)user_data;
		return c.SendCallback({(const std::byte *)data, length});
	}

	int OnFrameRecvCallback(const nghttp2_frame *frame) noexcept;

	static int OnFrameRecvCallback(nghttp2_session *,
				       const nghttp2_frame *frame,
				       void *user_data) noexcept {
		auto &c = *(ServerConnection *)user_data;
		return c.OnFrameRecvCallback(frame);
	}

	int OnBeginHeaderCallback(const nghttp2_frame *frame) noexcept;

	static int OnBeginHeaderCallback(nghttp2_session *,
					 const nghttp2_frame *frame,
					 void *user_data) noexcept {
		auto &c = *(ServerConnection *)user_data;
		return c.OnBeginHeaderCallback(frame);
	}

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedWrite() override;
	// TODO bool OnBufferedDrained() noexcept override;
	void OnBufferedError(std::exception_ptr e) noexcept override;
};

} // namespace NgHttp2
