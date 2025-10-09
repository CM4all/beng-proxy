// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "PerHttpStatusCounters.hxx"

#include <chrono>
#include <cstdint>

struct HttpStats {
	uint_least64_t n_requests = 0;

	/**
	 * The number of invalid HTTP/2 frames received.
	 *
	 * @see https://nghttp2.org/documentation/nghttp2_session_callbacks_set_on_invalid_frame_recv_callback.html
	 */
	uint_least64_t n_invalid_frames = 0;

	/**
	 * The number of HTTP requests that were rejected due to
	 * #BanList entry.
	 */
	uint_least64_t n_rejected = 0;

	/**
	 * The number of HTTP requests that were delayed due (for
	 * throttling/tarpit).
	 */
	uint_least64_t n_delayed = 0;

	uint_least64_t traffic_received = 0;
	uint_least64_t traffic_sent = 0;

	std::chrono::steady_clock::duration total_duration{};

	PerHttpStatusCounters n_per_status{};

	void AddRequest(HttpStatus status,
			uint_least64_t bytes_received,
			uint_least64_t bytes_sent,
			std::chrono::steady_clock::duration duration) noexcept {
		++n_requests;
		traffic_received += bytes_received;
		traffic_sent += bytes_sent;
		total_duration += duration;

		++n_per_status[HttpStatusToIndex(status)];
	}
};
