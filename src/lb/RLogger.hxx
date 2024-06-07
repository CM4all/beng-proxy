// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "http/Logger.hxx"

#include <chrono>
#include <cstdint>

struct HttpStats;
struct LbInstance;

/**
 * Attributes which are specific to the current request.  They are
 * only valid while a request is being handled (i.e. during the
 * lifetime of the #IncomingHttpRequest instance).  Strings are
 * allocated from the request pool.
 *
 * The request header pointers are here because our
 * http_client_request() call invalidates the original request
 * header StringMap instance, but after that, the access logger
 * needs these header values.
 */
struct LbRequestLogger final : IncomingHttpRequestLogger {
	LbInstance &instance;

	HttpStats &http_stats;

	/**
	 * The time stamp at the start of the request.  Used to calculate
	 * the request duration.
	 */
	const std::chrono::steady_clock::time_point start_time;

	/**
	 * The "Host" request header.
	 */
	const char *const host;

	/**
	 * The "X-Forwarded-For" request header.
	 */
	const char *const x_forwarded_for;

	/**
	 * The "Referer" [sic] request header.
	 */
	const char *const referer;

	/**
	 * The "User-Agent" request header.
	 */
	const char *const user_agent;

	/**
	 * The current request's canonical host name (from
	 * #TRANSLATE_CANONICAL_HOST).
	 */
	const char *canonical_host = nullptr;

	/**
	 * The name of the site being accessed by the current HTTP
	 * request (from #TRANSLATE_SITE).  It is a hack to allow the
	 * "log" callback to see this information.
	 */
	const char *site_name = nullptr;

	/**
	 * @see TranslationCommand::ANALYTICS_ID
	 */
	const char *analytics_id = nullptr;

	/**
	 * @see TranslationCommand::GENERATOR
	 */
	const char *generator = nullptr;

	/**
	 * @see LOG_FORWARDED_TO
	 */
	const char *forwarded_to = nullptr;

	/**
	 * Enable or disable the access logger.
	 */
	const bool access_logger;

	const bool access_logger_only_errors;

	LbRequestLogger(LbInstance &_instance, HttpStats &_http_stats,
			bool _access_logger,
			bool _access_logger_only_errors,
			const IncomingHttpRequest &request) noexcept;

	const char *GetCanonicalHost() const {
		return canonical_host != nullptr
			? canonical_host
			: host;
	}

	std::chrono::steady_clock::duration GetDuration(std::chrono::steady_clock::time_point now) const {
		return now - start_time;
	}

	/* virtual methods from class IncomingHttpRequestLogger */
	void LogHttpRequest(IncomingHttpRequest &request,
			    HttpStatus status,
			    Net::Log::ContentType content_type,
			    int_least64_t length,
			    uint_least64_t bytes_received,
			    uint_least64_t bytes_sent) noexcept override;
};
