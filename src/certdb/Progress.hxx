/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "util/Compiler.h"

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
