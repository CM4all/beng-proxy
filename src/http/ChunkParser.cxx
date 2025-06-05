// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ChunkParser.hxx"
#include "util/SpanCast.hxx"

#include <algorithm>
#include <stdexcept>
#include <utility> // for std::unreachable()

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
			std::unreachable();
		}
	}

	return AsBytes({p, 0});
}
