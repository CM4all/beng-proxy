// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "http/Logger.hxx"
#include "util/SharedLease.hxx"
#include "util/TokenBucket.hxx"

#include <chrono>
#include <cstdint>
#include <string_view>

struct BpInstance;
struct BpListenerStats;
class BpPerSite;
class AccessLogGlue;

/**
 * Attributes which are specific to the current request.  They are
 * only valid while a request is being handled (i.e. during the
 * lifetime of the #IncomingHttpRequest instance).  Strings are
 * allocated from the request pool.
 */
struct BpRequestLogger final : IncomingHttpRequestLogger {
	BpInstance &instance;

	BpListenerStats &http_stats;

	SharedLeasePtr<BpPerSite> per_site;
	TokenBucketConfig rate_limit_site_traffic{-1, -1};

	AccessLogGlue *const access_logger;

	/**
	 * The time stamp at the start of the request.  Used to calculate
	 * the request duration.
	 */
	const std::chrono::steady_clock::time_point start_time;

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
	 * From TranslationCommand::STATS_TAG
	 */
	std::string_view stats_tag{};

	const bool access_logger_only_errors;

	BpRequestLogger(BpInstance &_instance,
			BpListenerStats &_http_stats,
			AccessLogGlue *_access_logger,
			bool _access_logger_only_errors) noexcept;

	std::chrono::steady_clock::duration GetDuration(std::chrono::steady_clock::time_point now) const noexcept {
		return now - start_time;
	}

	/* virtual methods from class IncomingHttpRequestLogger */
	void LogHttpRequest(IncomingHttpRequest &request,
			    Event::Duration wait_duration,
			    HttpStatus status,
			    Net::Log::ContentType content_type,
			    int_least64_t length,
			    uint_least64_t bytes_received,
			    uint_least64_t bytes_sent) noexcept override;
};
