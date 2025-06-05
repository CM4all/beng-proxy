// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <span>
#include <utility>

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
