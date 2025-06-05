// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "http/ResponseHandler.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "event/DeferEvent.hxx"
#include "strmap.hxx"

/**
 * A #HttpResponseHandler implementation which stores the response and
 * uses #DeferEvent to forward it to the next #HttpResponseHandler
 * later.
 */
class DeferHttpResponseHandler final : public HttpResponseHandler {
	struct pool &pool;

	DeferEvent defer_event;

	HttpResponseHandler &next;

	HttpStatus status;
	StringMap headers;
	UnusedHoldIstreamPtr body;

	std::exception_ptr error;

public:
	DeferHttpResponseHandler(struct pool &_pool, EventLoop &event_loop,
				 HttpResponseHandler &_next) noexcept
		:pool(_pool),
		 defer_event(event_loop, BIND_THIS_METHOD(OnDeferred)),
		 next(_next) {}

private:
	void OnDeferred() noexcept {
		if (error)
			next.InvokeError(std::move(error));
		else
			next.InvokeResponse(status, std::move(headers),
					    std::move(body));
	}

protected:
	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus _status, StringMap &&_headers,
			    UnusedIstreamPtr _body) noexcept override {
		status = _status;
		headers = std::move(_headers);

		if (_body)
			body = UnusedHoldIstreamPtr{pool, std::move(_body)};

		defer_event.Schedule();
	}

	void OnHttpError(std::exception_ptr _error) noexcept override {
		error = std::move(_error);
		defer_event.Schedule();
	}
};
