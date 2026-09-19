// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "GlueHttpClient.hxx"
#include "http/Method.hxx"
#include "lib/curl/Easy.hxx"
#include "lib/curl/Slist.hxx"
#include "lib/curl/StringGlue.hxx"
#include "util/SpanCast.hxx"

#include <fmt/core.h>

inline CurlEasy
GlueHttpClient::PrepareRequest(HttpMethod method, const char *uri,
			       CurlSlist &header_list,
			       std::span<const std::byte> body)
{
	CurlEasy easy{uri};

	/* all URLs are supplied by the ACME server; restrict them to
	   HTTPS so that a malicious server cannot make us connect to
	   arbitrary services (SSRF) */
	easy.SetOption(CURLOPT_PROTOCOLS_STR, "https");
	easy.SetOption(CURLOPT_REDIR_PROTOCOLS_STR, "https");

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

Curl::StringResponse
GlueHttpClient::Request(HttpMethod method, const char *uri,
			std::span<const std::byte> body,
			Curl::StringOptions options)
{
	if (dump) {
		fmt::print(stderr, "{} {}\n",
			   http_method_to_string(method), uri);
		if (body.data() != nullptr) {
			fmt::print(stderr, "=== REQUEST BODY ===\n{}\n=== END ===\n", ToStringView(body));
		}
	}

	CurlSlist header_list;

	auto response = Curl::StringRequest(PrepareRequest(method, uri, header_list, body),
					    options);

	if (dump)
		fmt::print(stderr, "=== RESPONSE BODY ===\n{}\n=== END ===\n", ToStringView(response.body));

	return response;
}
