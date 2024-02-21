// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "GlueHttpClient.hxx"
#include "http/Method.hxx"
#include "lib/curl/Adapter.hxx"
#include "lib/curl/Easy.hxx"
#include "lib/curl/Slist.hxx"
#include "lib/curl/StringHandler.hxx"
#include "util/SpanCast.hxx"

#include <exception>

GlueHttpClient::GlueHttpClient(const char *_tls_ca)
	:tls_ca(_tls_ca)
{
}

GlueHttpClient::~GlueHttpClient()
{
}

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

StringCurlResponse
GlueHttpClient::Request(HttpMethod method, const char *uri,
			std::span<const std::byte> body)
{
	CurlSlist header_list;

	StringCurlResponseHandler handler;
	CurlResponseHandlerAdapter adapter{handler};

	auto easy = PrepareRequest(method, uri, header_list, body);
	adapter.Install(easy);

	CURLcode code = curl_easy_perform(easy.Get());
	adapter.Done(code);

	handler.CheckRethrowError();

	if (code != CURLE_OK)
		throw Curl::MakeError(code, "CURL error");

	return std::move(handler).GetResponse();
}
