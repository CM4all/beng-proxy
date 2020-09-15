/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "Socket.hxx"
#include "Control.hxx"
#include "Output.hxx"
#include "Input.hxx"
#include "pool/Ptr.hxx"
#include "http/Method.h"
#include "http/Status.h"

class EventLoop;
class UnusedIstreamPtr;
class StringMap;

class WasServerHandler {
public:
	virtual void OnWasRequest(struct pool &pool, http_method_t method,
				  const char *uri, StringMap &&headers,
				  UnusedIstreamPtr body) noexcept = 0;

	virtual void OnWasClosed() noexcept = 0;
};

class WasServer final : WasControlHandler, WasOutputHandler, WasInputHandler {
	struct pool &pool;

	WasSocket socket;

	WasControl control;

	WasServerHandler &handler;

	struct Request {
		PoolPtr pool;

		http_method_t method;

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
			 * Receiving headers.
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
		http_status_t status;

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

	void SendResponse(http_status_t status,
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

	void AbortError(const char *msg) noexcept;

	/**
	 * Abort receiving the response status/headers from the WAS server.
	 */
	void AbortUnused() noexcept;

	/* virtual methods from class WasControlHandler */
	bool OnWasControlPacket(enum was_command cmd,
				ConstBuffer<void> payload) noexcept override;
	bool OnWasControlDrained() noexcept override;
	void OnWasControlDone() noexcept override;
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
