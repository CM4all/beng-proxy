// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>

struct IstreamLength {
	/**
	 * Non-negative number of bytes which will be
	 * available in this #Istream (eventually).
	 */
	uint_least64_t length;

	/**
	 * True if the #Istream ends after #bytes.  False if
	 * an unknown number of bytes may follow (maybe zero).
	 */
	bool exhaustive;

	constexpr IstreamLength &operator+=(const IstreamLength &other) noexcept {
		length += other.length;
		exhaustive = exhaustive && other.exhaustive;
		return *this;
	}

	constexpr IstreamLength operator+(const IstreamLength &other) const noexcept {
		auto result = *this;
		result += other;
		return result;
	}
};
