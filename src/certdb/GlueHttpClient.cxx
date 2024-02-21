// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "GlueHttpClient.hxx"
#include "http/Method.hxx"
#include "lib/curl/Adapter.hxx"
#include "lib/curl/Easy.hxx"
#include "lib/curl/Handler.hxx"
#include "lib/curl/Slist.hxx"
#include "util/SpanCast.hxx"

#include <exception>

GlueHttpClient::GlueHttpClient(const char *_tls_ca)
	:tls_ca(_tls_ca)
{
}

GlueHttpClient::~GlueHttpClient()
{
}

class GlueHttpResponseHandler final : public CurlResponseHandler {
	HttpStatus status;
	Curl::Headers headers;

	std::string body_string;

	std::exception_ptr error;

public:
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
	}

	void OnError(std::exception_ptr e) noexcept override {
		error = std::move(e);
	}
};

inline CurlEasy
GlueHttpClient::PrepareRequest(HttpMethod method, const char *uri,
			       CurlSlist &header_list,
			       std::span<const std::byte> body)
{
	CurlEasy easy{uri};

	if (tls_ca != nullptr)
		easy.SetOption(CURLOPT_CAINFO, tls_ca);

	easy.SetOption(CURLOPT_VERBOSE, long(verbose));

	if (method == HttpMethod::HEAD)
		easy.SetNoBody();
	else if (method == HttpMethod::POST)
		easy.SetPost();

	if (body.data() != nullptr) {
		easy.SetRequestBody(body.data(), body.size_bytes());
		header_list.Append("Content-Type: application/jose+json");
	}

	easy.SetRequestHeaders(header_list.Get());

	return easy;
}

GlueHttpResponse
GlueHttpClient::Request(HttpMethod method, const char *uri,
			std::span<const std::byte> body)
{
	CurlSlist header_list;

	GlueHttpResponseHandler handler;
	CurlResponseHandlerAdapter adapter{handler};

	auto easy = PrepareRequest(method, uri, header_list, body);
	adapter.Install(easy);

	CURLcode code = curl_easy_perform(easy.Get());
	adapter.Done(code);

	handler.CheckThrowError();

	if (code != CURLE_OK)
		throw Curl::MakeError(code, "CURL error");

	return handler.MoveResponse();
}
