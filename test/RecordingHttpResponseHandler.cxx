// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "RecordingHttpResponseHandler.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "event/Loop.hxx"
#include "strmap.hxx"

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
RecordingHttpResponseHandler::OnHttpResponse(HttpStatus _status,
					     StringMap &&_headers,
					     UnusedIstreamPtr _body) noexcept
{
	assert(state == State::WAITING);
	assert(pool);

	status = _status;

	for (const auto &i : _headers)
		headers.emplace(i.key, i.value);

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
