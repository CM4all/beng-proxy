// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <algorithm>

/**
 * An interface to Workshop job progress reporting.
 */
class WorkshopProgress {
	unsigned min = 0, max = 0;

	bool use_control_channel = false;

public:
	WorkshopProgress() = default;

	constexpr WorkshopProgress(unsigned _min, unsigned _max) noexcept
		:min(_min), max(_max) {}

	constexpr WorkshopProgress(WorkshopProgress parent,
				   unsigned _min, unsigned _max) noexcept
		:min(parent.Scale(_min)), max(parent.Scale(_max)),
		 use_control_channel(parent.use_control_channel) {}

	void Enable(unsigned _min, unsigned _max) noexcept {
		min = _min;
		max = _max;
	}

	/**
	 * Send progress to the Workshop control channel on fd=3.
	 */
	void UseControlChannel() noexcept {
		use_control_channel = true;
	}

	constexpr bool IsEnabled() const noexcept {
		return min < max;
	}

	void operator()(int value) noexcept;

private:
	static constexpr unsigned Clamp(int x) noexcept {
		return std::min(100u, (unsigned)std::max(0, x));
	}

	constexpr unsigned Scale(unsigned x) const noexcept {
		return (min * (100u - x) + max * x) / 100u;
	}
};

/**
 * A simple wrapper for #WorkshopProgress which counts up to a
 * predefined number of steps.
 */
class StepProgress {
	WorkshopProgress parent;

	const unsigned n;

	unsigned i = 0;

public:
	StepProgress(WorkshopProgress _parent, unsigned _n) noexcept
		:parent(_parent), n(_n) {}

	void operator()() noexcept {
		++i;
		parent(i * 100u / n);
	}
};
