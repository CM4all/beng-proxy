// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Session.hxx"
#include "event/net/BufferedSocket.hxx"
#include "event/DeferEvent.hxx"
#include "util/IntrusiveList.hxx"

#include <cstdint>
#include <memory>

enum class HttpMethod : uint_least8_t;
class AllocatorPtr;
class StringMap;
class StopwatchPtr;
class HttpResponseHandler;
class UnusedIstreamPtr;
class CancellablePointer;
class FilteredSocket;

namespace NgHttp2 {

class ConnectionHandler {
public:
	virtual void OnNgHttp2ConnectionIdle() noexcept {}
	virtual void OnNgHttp2ConnectionGoAway() noexcept {}
	virtual void OnNgHttp2ConnectionError(std::exception_ptr e) noexcept = 0;
	virtual void OnNgHttp2ConnectionClosed() noexcept = 0;
};

class ClientConnection final : BufferedSocketHandler {
	static constexpr size_t MAX_CONCURRENT_STREAMS = 256;

	const std::unique_ptr<FilteredSocket> socket;

	ConnectionHandler &handler;

	NgHttp2::Session session;

	class Request;
	using RequestList = IntrusiveList<
		Request,
		IntrusiveListBaseHookTraits<Request>,
		IntrusiveListOptions{.constant_time_size = true}>;

	RequestList requests;

	DeferEvent defer_invoke_idle;

	size_t max_concurrent_streams = MAX_CONCURRENT_STREAMS;

#ifndef NDEBUG
	/**
	 * The total number of bytes passed to our
	 * data_chunk_recv_callback() which needs to be reported to
	 * nghttp2_session_consume().
	 */
	size_t unconsumed = 0;
#endif

public:
	ClientConnection(std::unique_ptr<FilteredSocket> socket,
			 ConnectionHandler &_handler);

	~ClientConnection() noexcept;

	auto &GetEventLoop() const noexcept {
		return defer_invoke_idle.GetEventLoop();
	}

	bool IsIdle() const noexcept {
		return requests.empty();
	}

	bool IsFull() const noexcept {
		return requests.size() >= max_concurrent_streams;
	}

	void SendRequest(AllocatorPtr alloc,
			 StopwatchPtr stopwatch,
			 HttpMethod method, const char *uri,
			 StringMap &&headers,
			 UnusedIstreamPtr body,
			 HttpResponseHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept;

private:
	void DeferWrite() noexcept;

	void InvokeIdle() noexcept {
		handler.OnNgHttp2ConnectionIdle();
	}

	void RemoveRequest(Request &request) noexcept;

	void AbortAllRequests(std::exception_ptr e) noexcept;

	ssize_t SendCallback(std::span<const std::byte> src) noexcept;

	static ssize_t SendCallback(nghttp2_session *, const uint8_t *data,
				    size_t length, int,
				    void *user_data) noexcept {
		auto &c = *(ClientConnection *)user_data;
		return c.SendCallback({(const std::byte *)data, length});
	}

	int OnFrameRecvCallback(const nghttp2_frame &frame) noexcept;

	static int OnFrameRecvCallback(nghttp2_session *,
				       const nghttp2_frame *frame,
				       void *user_data) noexcept {
		auto &c = *(ClientConnection *)user_data;
		return c.OnFrameRecvCallback(*frame);
	}

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedWrite() override;
	// TODO bool OnBufferedDrained() noexcept override;
	void OnBufferedError(std::exception_ptr e) noexcept override;
};

} // namespace NgHttp2
