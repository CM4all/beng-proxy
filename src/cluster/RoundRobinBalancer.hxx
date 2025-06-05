// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

class SocketAddress;
class Expiry;

/**
 * A round-robin load balancer for #AddressList.
 */
class RoundRobinBalancer final {
	/** the index of the item that will be returned next */
	unsigned next = 0;

public:
	/**
	 * Reset the state.  Call this after the list has been
	 * modified.
	 */
	void Reset() noexcept {
		next = 0;
	}

	template<typename List>
	typename List::const_reference Get(Expiry now, const List &list,
					   bool allow_fade) noexcept;

private:
	template<typename List>
	typename List::const_reference Next(const List &list) noexcept;
};
