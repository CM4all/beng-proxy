/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "HttpResponseHandler.hxx"
#include "istream/Handler.hxx"
#include "istream/StringSink.hxx"
#include "pool/Ptr.hxx"
#include "util/Cancellable.hxx"

#include <map>
#include <string>

class EventLoop;

struct RecordingHttpResponseHandler : HttpResponseHandler, StringSinkHandler {
	enum class State {
		WAITING,
		ERROR,
		NO_BODY,
		READING_BODY,
		BODY_ERROR,
		END,
	} state = State::WAITING;

	PoolPtr pool;
	EventLoop &event_loop;

	http_status_t status{};
	std::multimap<std::string, std::string> headers;
	std::string body;

	std::exception_ptr error;

	StringSink *body_sink = nullptr;
	CancellablePointer body_cancel_ptr;

	RecordingHttpResponseHandler(struct pool &parent_pool,
				     EventLoop &_event_loop) noexcept;

	bool IsAlive() const noexcept {
		switch (state) {
		case State::WAITING:
		case State::READING_BODY:
			return true;

		case State::ERROR:
		case State::NO_BODY:
		case State::BODY_ERROR:
		case State::END:
			return false;
		}

		return false;
	}

	void ReadBody() noexcept;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(http_status_t _status, StringMap &&_headers,
			    UnusedIstreamPtr) noexcept override;
	void OnHttpError(std::exception_ptr _error) noexcept override;

	/* virtual methods from class StringSinkHandler */
	void OnStringSinkSuccess(std::string &&_value) noexcept final;
	void OnStringSinkError(std::exception_ptr _error) noexcept final;
};
