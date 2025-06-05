// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "http/ResponseHandler.hxx"
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

	HttpStatus status{};
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
	void OnHttpResponse(HttpStatus _status, StringMap &&_headers,
			    UnusedIstreamPtr) noexcept override;
	void OnHttpError(std::exception_ptr _error) noexcept override;

	/* virtual methods from class StringSinkHandler */
	void OnStringSinkSuccess(std::string &&_value) noexcept final;
	void OnStringSinkError(std::exception_ptr _error) noexcept final;
};
