// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "http/StatusIndex.hxx"

#include <array>
#include <chrono>
#include <cstdint>

struct HttpStats {
	uint64_t n_requests = 0;
	uint64_t traffic_received = 0;
	uint64_t traffic_sent = 0;

	std::chrono::steady_clock::duration total_duration{};

	std::array<uint64_t, valid_http_status_array.size()> n_per_status{};

	void AddRequest(HttpStatus status,
			uint64_t bytes_received,
			uint64_t bytes_sent,
			std::chrono::steady_clock::duration duration) noexcept {
		++n_requests;
		traffic_received += bytes_received;
		traffic_sent += bytes_sent;
		total_duration += duration;

		++n_per_status[HttpStatusToIndex(status)];
	}
};
