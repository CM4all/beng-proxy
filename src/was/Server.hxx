// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Output.hxx"
#include "Input.hxx"
#include "was/async/Control.hxx"
#include "was/async/Socket.hxx"
#include "pool/Ptr.hxx"
#include "util/StringBuffer.hxx"

enum class HttpMethod : uint_least8_t;
enum class HttpStatus : uint_least16_t;
class EventLoop;
class UnusedIstreamPtr;
class StringMap;

class WasServerHandler {
public:
	virtual void OnWasRequest(struct pool &pool, HttpMethod method,
				  const char *uri, StringMap &&headers,
				  UnusedIstreamPtr body) noexcept = 0;

	virtual void OnWasClosed() noexcept = 0;
};

class WasServer final : Was::ControlHandler, WasOutputHandler, WasInputHandler {
	struct pool &pool;

	WasSocket socket;

	Was::Control control;

	WasServerHandler &handler;

	struct Request {
		PoolPtr pool;

		HttpMethod method;

		const char *uri;

		/**
		 * Request headers being assembled.  This pointer is set to
		 * nullptr before before the request is dispatched to the
		 * handler.
		 */
		StringMap *headers;

		WasInput *body;

		bool released = false;

		enum class State : uint8_t {
			/**
			 * No request is being processed currently.
			 */
			NONE,

			/**
			 * Receiving headers.
			 */
			HEADERS,

			/**
			 * Pending call to WasServerHandler::OnWasRequest().
			 */
			PENDING,

			/**
			 * Request metadata already submitted to
			 * WasServerHandler::OnWasRequest().
			 */
			SUBMITTED,
		} state = State::NONE;
	} request;

	struct {
		HttpStatus status;

		StringBuffer<32> content_length_buffer;

		WasOutput *body;
	} response;

public:
	/**
	 * Creates a WAS server, waiting for HTTP requests on the
	 * specified socket.
	 *
	 * @param _pool the memory pool
	 * @param _control_fd a control socket to the WAS client
	 * @param _input_fd a data pipe for the request body
	 * @param _output_fd a data pipe for the response body
	 * @param _handler a callback function which receives events
	 */
	WasServer(struct pool &_pool, EventLoop &event_loop,
		  WasSocket &&_socket,
		  WasServerHandler &_handler) noexcept;

	void Free() noexcept {
		ReleaseError("shutting down WAS connection");
	}

	auto &GetEventLoop() const noexcept {
		return control.GetEventLoop();
	}

	void SendResponse(HttpStatus status,
			  StringMap &&headers, UnusedIstreamPtr body) noexcept;

private:
	void Destroy() noexcept {
		this->~WasServer();
	}

	void ReleaseError(std::exception_ptr ep) noexcept;
	void ReleaseError(const char *msg) noexcept;

	void ReleaseUnused() noexcept;

	/**
	 * Abort receiving the response status/headers from the WAS server.
	 */
	void AbortError(std::exception_ptr ep) noexcept;

	void AbortProtocolError(const char *msg) noexcept;

	/**
	 * Abort receiving the response status/headers from the WAS server.
	 */
	void AbortUnused() noexcept;

	/* virtual methods from class Was::ControlHandler */
	bool OnWasControlPacket(enum was_command cmd,
				std::span<const std::byte> payload) noexcept override;
	bool OnWasControlDrained() noexcept override;
	void OnWasControlDone() noexcept override;
	void OnWasControlHangup() noexcept override;
	void OnWasControlError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class WasOutputHandler */
	bool WasOutputLength(uint64_t length) noexcept override;
	bool WasOutputPremature(uint64_t length,
				std::exception_ptr ep) noexcept override;
	void WasOutputEof() noexcept override;
	void WasOutputError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class WasInputHandler */
	void WasInputClose(uint64_t received) noexcept override;
	bool WasInputRelease() noexcept override;
	void WasInputEof() noexcept override;
	void WasInputError() noexcept override;
};
