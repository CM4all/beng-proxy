// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "http/Logger.hxx"

#include <chrono>
#include <string_view>

#include <stdint.h>

struct BpInstance;
struct TaggedHttpStats;

/**
 * Attributes which are specific to the current request.  They are
 * only valid while a request is being handled (i.e. during the
 * lifetime of the #IncomingHttpRequest instance).  Strings are
 * allocated from the request pool.
 */
struct BpRequestLogger final : IncomingHttpRequestLogger {
	BpInstance &instance;

	TaggedHttpStats &http_stats;

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

	/**
	 * Enable or disable the access logger.
	 */
	const bool access_logger;

	const bool access_logger_only_errors;

	BpRequestLogger(BpInstance &_instance,
			TaggedHttpStats &_http_stats,
			bool _access_logger,
			bool _access_logger_only_errors) noexcept;

	std::chrono::steady_clock::duration GetDuration(std::chrono::steady_clock::time_point now) const noexcept {
		return now - start_time;
	}

	/* virtual methods from class IncomingHttpRequestLogger */
	void LogHttpRequest(IncomingHttpRequest &request,
			    HttpStatus status,
			    Net::Log::ContentType content_type,
			    int64_t length,
			    uint64_t bytes_received,
			    uint64_t bytes_sent) noexcept override;
};
