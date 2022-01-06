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

/**
 * An iterable range of ALPN strrings.
 */
class AlpnRange {
	using Span = std::span<const unsigned char>;
	Span s;

public:
	constexpr AlpnRange(Span _s) noexcept
		:s(_s) {}

	class const_iterator {
		Span current, rest;

	public:
		const_iterator(Span _current, Span _rest) noexcept
			:current(_current), rest(_rest) {}

		const_iterator(std::pair<Span, Span> p) noexcept
			:const_iterator(p.first, p.second) {}

		constexpr Span operator*() const noexcept {
			return current;
		}

		constexpr const_iterator &operator++() noexcept {
			auto p = Split(rest);
			current = p.first;
			rest = p.second;
			return *this;
		}

		bool operator==(const_iterator other) const noexcept {
			return current.data() == other.current.data();
		}
	};

	const_iterator begin() const noexcept {
		return Split(s);
	}

	const_iterator end() const noexcept {
		return {s.subspan(s.size()), s};
	}

private:
	static constexpr std::pair<Span, Span> Split(Span s) noexcept {
		if (s.empty())
			return {s.subspan(s.size()), s};

		std::size_t size = s.front() + 1;
		if (s.size() < size)
			return {s.subspan(s.size()), s};

		return {s.subspan(0, size), s.subspan(size)};
	}
};
