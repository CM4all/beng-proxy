// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "lib/curl/Headers.hxx"

#include <cstdint>
#include <span>
#include <string>

enum class HttpMethod : uint_least8_t;
enum class HttpStatus : uint_least16_t;
class CurlSlist;
class CurlEasy;

struct GlueHttpResponse {
	HttpStatus status;

	Curl::Headers headers;

	std::string body;

	GlueHttpResponse(HttpStatus _status,
			 Curl::Headers &&_headers,
			 std::string &&_body)
		:status(_status), headers(std::move(_headers)), body(_body) {}
};

class GlueHttpClient {
	const char *const tls_ca;

	bool verbose = false;

public:
	GlueHttpClient(const char *_tls_ca);
	~GlueHttpClient();

	GlueHttpClient(const GlueHttpClient &) = delete;
	GlueHttpClient &operator=(const GlueHttpClient &) = delete;

	void EnableVerbose() noexcept {
		verbose = true;
	}

	GlueHttpResponse Request(HttpMethod method, const char *uri,
				 std::span<const std::byte> body);

private:
	CurlEasy PrepareRequest(HttpMethod method, const char *uri,
				CurlSlist &header_list,
				std::span<const std::byte> body);
};
