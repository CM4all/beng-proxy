// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "GlueHttpClient.hxx"
#include "http/Method.hxx"
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

	HttpStatus status;
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

	void OnHeaders(HttpStatus _status, Curl::Headers &&_headers) override {
		status = _status;
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
			HttpMethod method, const char *uri,
			std::span<const std::byte> body)
{
	CurlSlist header_list;

	GlueHttpResponseHandler handler{event_loop};
	CurlRequest request(curl_global, uri, handler);

	request.SetOption(CURLOPT_VERBOSE, long(verbose));

	if (method == HttpMethod::HEAD)
		request.SetNoBody();
	else if (method == HttpMethod::POST)
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
