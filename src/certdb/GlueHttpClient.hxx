// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "lib/curl/StringResponse.hxx"

#include <cstdint>
#include <span>

enum class HttpMethod : uint_least8_t;
enum class HttpStatus : uint_least16_t;
class CurlSlist;
class CurlEasy;

class GlueHttpClient {
	const char *const tls_ca;

	bool verbose = false;

public:
	explicit GlueHttpClient(const char *_tls_ca) noexcept
		:tls_ca(_tls_ca) {}

	GlueHttpClient(const GlueHttpClient &) = delete;
	GlueHttpClient &operator=(const GlueHttpClient &) = delete;

	void EnableVerbose() noexcept {
		verbose = true;
	}

	StringCurlResponse Request(HttpMethod method, const char *uri,
				   std::span<const std::byte> body);

private:
	CurlEasy PrepareRequest(HttpMethod method, const char *uri,
				CurlSlist &header_list,
				std::span<const std::byte> body);
};
