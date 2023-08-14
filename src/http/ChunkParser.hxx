// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cassert>
#include <cstddef>
#include <span>

/**
 * Incremental parser for "Transfer-Encoding:chunked".
 */
class HttpChunkParser {
	enum class State {
		NONE,
		SIZE,
		AFTER_SIZE,
		DATA,
		AFTER_DATA,
		TRAILER,
		TRAILER_DATA,
		END,
	} state = State::NONE;

	size_t remaining_chunk;

public:
	constexpr bool HasEnded() const noexcept {
		return state == State::END;
	}

	/**
	 * Find the next data chunk.
	 *
	 * Throws exception on error.
	 *
	 * @return a pointer to the data chunk, an empty chunk pointing to
	 * the end of input if there is no data chunk
	 */
	std::span<const std::byte> Parse(std::span<const std::byte> input);

	constexpr bool Consume(size_t nbytes) noexcept {
		assert(nbytes > 0);
		assert(state == State::DATA);
		assert(nbytes <= remaining_chunk);

		remaining_chunk -= nbytes;

		bool finished = remaining_chunk == 0;
		if (finished)
			state = State::AFTER_DATA;
		return finished;
	}

	constexpr size_t GetAvailable() const noexcept {
		return state == State::DATA
			? remaining_chunk
			: 0;
	}
};
