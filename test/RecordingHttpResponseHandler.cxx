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

#include "RecordingHttpResponseHandler.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "event/Loop.hxx"

#include <assert.h>

RecordingHttpResponseHandler::RecordingHttpResponseHandler(struct pool &parent_pool,
							   EventLoop &_event_loop) noexcept
	:pool(pool_new_libc(&parent_pool,
			    "RecordingHttpResponseHandler")),
	 event_loop(_event_loop) {}

void
RecordingHttpResponseHandler::ReadBody() noexcept
{
	assert(body_sink != nullptr);

	ReadStringSink(*body_sink);
}

void
RecordingHttpResponseHandler::OnHttpResponse(http_status_t _status, StringMap &&,
					     UnusedIstreamPtr _body) noexcept
{
	assert(state == State::WAITING);
	assert(pool);

	status = _status;

	if (_body) {
		state = State::READING_BODY;
		body_sink = &NewStringSink(pool, std::move(_body), *this,
					   body_cancel_ptr);
		ReadBody();
	} else {
		state = State::NO_BODY;
		event_loop.Break();
		pool.reset();
	}
}

void
RecordingHttpResponseHandler::OnHttpError(std::exception_ptr _error) noexcept
{
	assert(state == State::WAITING);
	assert(pool);

	error = std::move(_error);
	state = State::ERROR;
	event_loop.Break();
	pool.reset();
}

void
RecordingHttpResponseHandler::OnStringSinkSuccess(std::string &&_value) noexcept
{
	assert(state == State::READING_BODY);
	assert(body_sink != nullptr);

	body_sink = nullptr;
	body = std::move(_value);
	state = State::END;
	event_loop.Break();
	pool.reset();
}

void
RecordingHttpResponseHandler::OnStringSinkError(std::exception_ptr _error) noexcept
{
	assert(state == State::READING_BODY);
	assert(body_sink != nullptr);

	body_sink = nullptr;
	error = std::move(_error);
	state = State::BODY_ERROR;
	event_loop.Break();
	pool.reset();
}
