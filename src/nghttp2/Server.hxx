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

	/**
	 * A timer which closes the connection if there is no request
	 * (HTTP/2 stream) for some time.  The timer is scheduled when
	 * the #requests list becomes empty (and is canceled when a
	 * new request is added to #requests).
	 */
	CoarseTimerEvent idle_timer;

	static constexpr Event::Duration idle_timeout = std::chrono::minutes{2};

public:
	ServerConnection(struct pool &_pool,
			 UniquePoolPtr<FilteredSocket> _socket,
			 SocketAddress remote_address,
			 HttpServerConnectionHandler &_handler,
			 HttpServerRequestHandler &request_handler);

	~ServerConnection() noexcept;

	EventLoop &GetEventLoop() const noexcept {
		return idle_timer.GetEventLoop();
	}

	void Consume(std::size_t nbytes) noexcept {
		nghttp2_session_consume_connection(session.get(),
						   nbytes);
		DeferWrite();
	}

	void RemoveRequest(Request &request) noexcept;

private:
	void DeferWrite() noexcept;

	ssize_t SendCallback(std::span<const std::byte> src) noexcept;

	static ssize_t SendCallback(nghttp2_session *, const uint8_t *data,
				    size_t length, int,
				    void *user_data) noexcept {
		auto &c = *(ServerConnection *)user_data;
		return c.SendCallback({(const std::byte *)data, length});
	}

	int OnFrameRecvCallback(const nghttp2_frame &frame) noexcept;

	static int OnFrameRecvCallback(nghttp2_session *,
				       const nghttp2_frame *frame,
				       void *user_data) noexcept {
		auto &c = *(ServerConnection *)user_data;
		return c.OnFrameRecvCallback(*frame);
	}

	int OnFrameSendCallback(const nghttp2_frame &frame) noexcept;

	static int OnFrameSendCallback(nghttp2_session *,
				       const nghttp2_frame *frame,
				       void *user_data) noexcept {
		auto &c = *static_cast<ServerConnection *>(user_data);
		return c.OnFrameSendCallback(*frame);
	}

	int OnBeginHeaderCallback(const nghttp2_frame &frame) noexcept;

	static int OnBeginHeaderCallback(nghttp2_session *,
					 const nghttp2_frame *frame,
					 void *user_data) noexcept {
		auto &c = *(ServerConnection *)user_data;
		return c.OnBeginHeaderCallback(*frame);
	}

	int OnInvalidFrameReceivedCallback(const nghttp2_frame &frame,
					   int lib_error_code) noexcept;

	static int OnInvalidFrameReceivedCallback(nghttp2_session *,
						  const nghttp2_frame *frame,
						  int lib_error_code,
						  void *user_data) noexcept {
		auto &c = *(ServerConnection *)user_data;
		return c.OnInvalidFrameReceivedCallback(*frame, lib_error_code);
	}

	void OnIdleTimeout() noexcept;

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedWrite() override;
	// TODO bool OnBufferedDrained() noexcept override;
	void OnBufferedError(std::exception_ptr e) noexcept override;
};

} // namespace NgHttp2
