// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Glue code for the logging protocol.
 */

#pragma once

#include "Config.hxx"

#include <chrono>
#include <memory>

#include <stdint.h>

enum class HttpStatus : uint_least16_t;
struct UidGid;
struct AccessLogConfig;
namespace Net::Log { enum class ContentType : uint8_t; struct Datagram; class Sink; }
struct IncomingHttpRequest;
class SocketDescriptor;
class EventLoop;
class LogClient;

class AccessLogGlue {
	const AccessLogConfig config;

	const std::unique_ptr<LogClient> client;

	AccessLogGlue(const AccessLogConfig &config,
		      std::unique_ptr<LogClient> _client) noexcept;

public:
	~AccessLogGlue() noexcept;

	static AccessLogGlue *Create(EventLoop &event_loop,
				     const AccessLogConfig &config,
				     const UidGid *user);

	void Log(const Net::Log::Datagram &d) noexcept;

	/**
	 * @param length the number of response body (payload) bytes sent
	 * to our HTTP client or negative if there was no response body
	 * (which is different from "empty response body")
	 * @param bytes_received the number of raw bytes received from our
	 * HTTP client
	 * @param bytes_sent the number of raw bytes sent to our HTTP client
	 * (which includes status line, headers and transport encoding
	 * overhead such as chunk headers)
	 */
	void Log(std::chrono::system_clock::time_point now,
		 const IncomingHttpRequest &request, const char *site,
		 const char *analytics_id,
		 const char *generator,
		 const char *forwarded_to,
		 const char *host, const char *x_forwarded_for,
		 const char *referer, const char *user_agent,
		 HttpStatus status,
		 Net::Log::ContentType content_type,
		 int64_t length,
		 uint64_t bytes_received, uint64_t bytes_sent,
		 std::chrono::steady_clock::duration duration) noexcept;

	void Log(std::chrono::system_clock::time_point now,
		 const IncomingHttpRequest &request, const char *site,
		 const char *analytics_id,
		 const char *generator,
		 const char *forwarded_to,
		 const char *referer, const char *user_agent,
		 HttpStatus status,
		 Net::Log::ContentType content_type,
		 int64_t length,
		 uint64_t bytes_received, uint64_t bytes_sent,
		 std::chrono::steady_clock::duration duration) noexcept;

	/**
	 * Returns the connected log sink to be used to send child
	 * process error messages.  Returns SocketDescriptor::Undefined()
	 * if the feature is disabled.
	 */
	Net::Log::Sink *GetChildSink() noexcept;
};
