// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "stats/TaggedHttpStats.hxx"

/**
 * Per-listener statistics.
 */
struct BpListenerStats {
	TaggedHttpStats tagged;

	void AddRequest(std::string_view tag,
			HttpStatus status,
			uint64_t bytes_received,
			uint64_t bytes_sent,
			std::chrono::steady_clock::duration duration) noexcept {
		tagged.AddRequest(tag, status,
				  bytes_received, bytes_sent,
				  duration);
	}
};
