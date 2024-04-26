// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstdint>
#include <string_view>

class GrowingBuffer;
namespace BengControl { struct Stats; }

namespace Prometheus {

struct Stats {
	/**
	 * Number of open incoming connections.
	 */
	uint_least32_t incoming_connections;

	/**
	 * Number of open outgoing connections.
	 */
	uint_least32_t outgoing_connections;

	/**
	 * Number of child processes.
	 */
	uint_least32_t children;

	/**
	 * Number of sessions.
	 */
	uint_least32_t sessions;

	/**
	 * Total number of incoming HTTP requests that were received since
	 * the server was started.
	 */
	uint_least64_t http_requests;

	/**
	 * In- and outgoing HTTP traffic since
	 * the server was started.
	 */
	uint_least64_t http_traffic_received, http_traffic_sent;

	/**
	 * The total allocated size of the translation cache in the
	 * server's memory [bytes].
	 */
	uint_least64_t translation_cache_brutto_size, translation_cache_size;

	/**
	 * The total allocated size of the HTTP cache in the server's
	 * memory [bytes].
	 */
	uint_least64_t http_cache_brutto_size, http_cache_size;

	/**
	 * The total allocated size of the filter cache in the server's
	 * memory [bytes].
	 */
	uint_least64_t filter_cache_brutto_size, filter_cache_size;

	/**
	 * Total size of I/O buffers.
	 */
	uint_least64_t io_buffers_brutto_size, io_buffers_size;
};

void
Write(GrowingBuffer &buffer, std::string_view process,
      const Stats &stats) noexcept;

} // namespace Prometheus
