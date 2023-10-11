// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "time/Cast.hxx"
#include "util/TokenBucket.hxx"

#include <chrono>

/**
 * Kepe track of certain abuses (like too many HTTP/2 RST_STREAM or
 * exceeding the maximum number of streams; aka "HTTP/2 Rapid Reset").
 * After too many abuses, new requests will be delayed.
 */
class AbuseTarpit {
	static constexpr double RATE = 10;
	static constexpr double BURST = 100;

	static constexpr std::chrono::steady_clock::duration DURATION = std::chrono::seconds{20};
	static constexpr std::chrono::steady_clock::duration DELAY = std::chrono::seconds{5};

	// TODO: a TokenBucket is not the right algorithm here
	TokenBucket rate_limiter;

	std::chrono::steady_clock::time_point tarpit_until;

public:
	constexpr void Record(std::chrono::steady_clock::time_point now, double size=1) noexcept {
		const auto float_now = ToFloatSeconds(now.time_since_epoch());
		if (!rate_limiter.Check(float_now, RATE, BURST, size))
			tarpit_until = now + DURATION;
	}

	constexpr std::chrono::steady_clock::duration GetDelay(std::chrono::steady_clock::time_point now) const noexcept {
		return now < tarpit_until
			? DELAY
			: std::chrono::steady_clock::duration{};
	}
};
