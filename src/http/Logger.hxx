// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/Chrono.hxx"

#include <cstdint>

enum class HttpStatus : uint_least16_t;
namespace Net::Log { enum class ContentType : uint8_t; }
struct IncomingHttpRequest;

class IncomingHttpRequestLogger {
	const bool want_content_type;

protected:
	explicit IncomingHttpRequestLogger(bool _want_content_type) noexcept
		:want_content_type(_want_content_type) {}

public:
	virtual ~IncomingHttpRequestLogger() noexcept = default;

	/**
	 * Is this instance interested in getting the parsed
	 * Content-Type response header?  If not, then the caller can
	 * omit the call to Net::Log::ParseContentType().
	 */
	bool WantsContentType() const noexcept {
		return want_content_type;
	}

	/**
	 * @param wait_duration the total duration waiting for the
	 * client (either request body data or response body)
	 *
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
				    Event::Duration wait_duration,
				    HttpStatus status,
				    Net::Log::ContentType content_type,
				    int64_t length,
				    uint64_t bytes_received, uint64_t bytes_sent) noexcept = 0;
};
