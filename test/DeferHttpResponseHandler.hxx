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

	http_status_t status;
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
	void OnHttpResponse(http_status_t _status, StringMap &&_headers,
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
