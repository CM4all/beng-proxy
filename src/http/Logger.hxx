// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstdint>

enum class HttpStatus : uint_least16_t;
namespace Net::Log { enum class ContentType : uint8_t; }
struct IncomingHttpRequest;

class IncomingHttpRequestLogger {
public:
	virtual ~IncomingHttpRequestLogger() noexcept = default;

	/**
	 * @param length the number of response body (payload) bytes sent
	 * to our HTTP client, or negative if there was no response body
	 * (which is different from "empty response body")
	 * @param bytes_received the number of raw bytes received from our
	 * HTTP client
	 * @param bytes_sent the number of raw bytes sent to our HTTP
	 * client (which includes status line, headers and transport
	 * encoding overhead such as chunk headers)
	 */
	virtual void LogHttpRequest(IncomingHttpRequest &request,
				    HttpStatus status,
				    Net::Log::ContentType content_type,
				    int64_t length,
				    uint64_t bytes_received, uint64_t bytes_sent) noexcept = 0;
};
