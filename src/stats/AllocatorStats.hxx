// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstddef>

struct AllocatorStats {
	/**
	 * Number of bytes allocated from the kernel.
	 */
	std::size_t brutto_size;

	/**
	 * Number of bytes being used by client code.
	 */
	std::size_t netto_size;

	static constexpr AllocatorStats Zero() {
		return { 0, 0 };
	}

	void Clear() {
		brutto_size = 0;
		netto_size = 0;
	}

	AllocatorStats &operator+=(const AllocatorStats other) {
		brutto_size += other.brutto_size;
		netto_size += other.netto_size;
		return *this;
	}

	constexpr AllocatorStats operator+(const AllocatorStats other) const {
		return { brutto_size + other.brutto_size,
			netto_size + other.netto_size };
	}
};
