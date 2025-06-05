// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "stats/CacheStats.hxx"
#include "memory/AllocatorStats.hxx"

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

	CacheStats translation_cache, http_cache, filter_cache, encoding_cache;

	AllocatorStats io_buffers;
};

void
Write(GrowingBuffer &buffer, std::string_view process,
      const Stats &stats) noexcept;

} // namespace Prometheus
