// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "GlueHttpClient.hxx"
#include "http/Method.hxx"
#include "lib/curl/Easy.hxx"
#include "lib/curl/Slist.hxx"
#include "lib/curl/StringGlue.hxx"

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
		easy.SetRequestBody(body);
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

	return StringCurlRequest(PrepareRequest(method, uri, header_list, body));
}
