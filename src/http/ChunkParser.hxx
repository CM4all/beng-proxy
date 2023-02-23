// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/Compiler.h"
#include "util/SpanCast.hxx"

#include <cassert>
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <span>
#include <string_view>

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
	bool HasEnded() const {
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

	bool Consume(size_t nbytes) {
		assert(nbytes > 0);
		assert(state == State::DATA);
		assert(nbytes <= remaining_chunk);

		remaining_chunk -= nbytes;

		bool finished = remaining_chunk == 0;
		if (finished)
			state = State::AFTER_DATA;
		return finished;
	}

	size_t GetAvailable() const {
		return state == State::DATA
			? remaining_chunk
			: 0;
	}
};

std::span<const std::byte>
HttpChunkParser::Parse(std::span<const std::byte> _input)
{
	const auto input = ToStringView(_input);
	auto p = input.begin();
	const auto end = input.end();
	size_t digit;

	while (p != end) {
		assert(p < end);

		const auto ch = *p;
		switch (state) {
		case State::NONE:
		case State::SIZE:
			if (ch >= '0' && ch <= '9') {
				digit = ch - '0';
			} else if (ch >= 'a' && ch <= 'f') {
				digit = ch - 'a' + 0xa;
			} else if (ch >= 'A' && ch <= 'F') {
				digit = ch - 'A' + 0xa;
			} else if (state == State::SIZE) {
				state = State::AFTER_SIZE;
				continue;
			} else {
				throw std::runtime_error("chunk length expected");
			}

			if (state == State::NONE) {
				state = State::SIZE;
				remaining_chunk = 0;
			}

			++p;
			remaining_chunk = remaining_chunk * 0x10 + digit;
			break;

		case State::AFTER_SIZE:
			if (ch == '\n') {
				if (remaining_chunk == 0)
					state = State::TRAILER;
				else
					state = State::DATA;
			}

			++p;
			break;

		case State::DATA:
			assert(remaining_chunk > 0);

			return AsBytes({p, std::min(size_t(end - p), remaining_chunk)});

		case State::AFTER_DATA:
			if (ch == '\n') {
				state = State::NONE;
			} else if (ch != '\r') {
				throw std::runtime_error("newline expected");
			}

			++p;
			break;

		case State::TRAILER:
			++p;
			if (ch == '\n') {
				state = State::END;
				return AsBytes({p, 0});
			} else if (ch != '\r') {
				state = State::TRAILER_DATA;
			}
			break;

		case State::TRAILER_DATA:
			++p;
			if (ch == '\n')
				state = State::TRAILER;
			break;

		case State::END:
			assert(false);
			gcc_unreachable();
		}
	}

	return AsBytes({p, 0});
}
