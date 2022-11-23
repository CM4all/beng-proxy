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

#include "GlueHttpClient.hxx"
#include "event/Loop.hxx"
#include "lib/curl/Request.hxx"
#include "lib/curl/Handler.hxx"
#include "lib/curl/Slist.hxx"
#include "util/SpanCast.hxx"

#include <exception>

GlueHttpClient::GlueHttpClient(EventLoop &event_loop)
	:curl_global(event_loop)
{
}

GlueHttpClient::~GlueHttpClient()
{
}

class GlueHttpResponseHandler final : public CurlResponseHandler {
	EventLoop &event_loop;

	http_status_t status;
	Curl::Headers headers;

	std::string body_string;

	std::exception_ptr error;

	bool done = false;

public:
	explicit GlueHttpResponseHandler(EventLoop &_event_loop) noexcept
		:event_loop(_event_loop) {}

	bool IsDone() const {
		return done;
	}

	void CheckThrowError() {
		if (error)
			std::rethrow_exception(error);
	}

	GlueHttpResponse MoveResponse() {
		return {status, std::move(headers), std::move(body_string)};
	}

public:
	/* virtual methods from class CurlResponseHandler */

	void OnHeaders(unsigned _status, Curl::Headers &&_headers) override {
		status = http_status_t(_status);
		headers = std::move(_headers);
	}

	void OnData(std::span<const std::byte> data) override {
		body_string.append(ToStringView(data));
	}

	void OnEnd() override {
		done = true;
		event_loop.Break();
	}

	void OnError(std::exception_ptr e) noexcept override {
		error = std::move(e);
		done = true;
		event_loop.Break();
	}
};

GlueHttpResponse
GlueHttpClient::Request(EventLoop &event_loop,
			http_method_t method, const char *uri,
			std::span<const std::byte> body)
{
	CurlSlist header_list;

	GlueHttpResponseHandler handler{event_loop};
	CurlRequest request(curl_global, uri, handler);

	request.SetOption(CURLOPT_VERBOSE, long(verbose));

	if (method == HTTP_METHOD_HEAD)
		request.SetNoBody();
	else if (method == HTTP_METHOD_POST)
		request.SetPost();

	if (body.data() != nullptr) {
		request.SetRequestBody(body.data(), body.size_bytes());
		header_list.Append("Content-Type: application/jose+json");
	}

	request.SetRequestHeaders(header_list.Get());

	request.Start();

	if (!handler.IsDone())
		event_loop.Run();

	assert(handler.IsDone());

	handler.CheckThrowError();
	return handler.MoveResponse();
}
