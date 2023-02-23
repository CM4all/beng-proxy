// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
