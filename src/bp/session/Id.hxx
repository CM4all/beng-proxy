// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Prng.hxx"
#include "cluster/StickyHash.hxx"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

template<std::size_t> class StringBuffer;

/**
 * The session id data structure.
 */
class SessionId {
	std::array<uint64_t, 2> data;

public:
	[[gnu::pure]]
	bool IsDefined() const noexcept {
		return std::any_of(data.begin(), data.end(), [](auto i){
			return i != 0;
		});
	}

	void Clear() noexcept {
		std::fill(data.begin(), data.end(), 0);
	}

	template<typename Engine>
	void Generate(Engine &prng) noexcept {
		static_assert(Engine::word_size == sizeof(data.front()) * 8);

		for (auto &i : data)
			i = prng();
	}

	/**
	 * Manipulate the modulo of GetClusterHash() so that it results in
	 * the specified cluster node.
	 */
	void SetClusterNode(unsigned cluster_size, unsigned cluster_node) noexcept;

	[[gnu::pure]]
	bool operator==(const SessionId &other) const noexcept {
		return std::equal(data.begin(), data.end(), other.data.begin());
	}

	[[gnu::pure]]
	bool operator!=(const SessionId &other) const noexcept {
		return !(*this == other);
	}

	[[gnu::pure]]
	std::size_t Hash() const noexcept {
		return data[0];
	}

	/**
	 * Returns a hash that can be used to determine the cluster node
	 * by calculating the modulo.
	 */
	[[gnu::pure]]
	sticky_hash_t GetClusterHash() const noexcept {
		/* truncating to 32 bit because that is what beng-lb's
		   lb_session_get() function uses */
		return static_cast<sticky_hash_t>(data.back());
	}

	/**
	 * Parse a session id from a string.
	 *
	 * @return true on success, false on error
	 */
	bool Parse(std::string_view s) noexcept;

	[[gnu::pure]]
	StringBuffer<sizeof(data) * 2 + 1> Format() const noexcept;
};
