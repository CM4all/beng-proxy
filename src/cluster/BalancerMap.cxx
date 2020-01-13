/*
 * Copyright 2007-2020 Content Management AG
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

#include "BalancerMap.hxx"
#include "HashKey.hxx"
#include "PickGeneric.hxx"
#include "RoundRobinBalancer.cxx"
#include "AddressList.hxx"
#include "net/SocketAddress.hxx"
#include "net/FailureManager.hxx"

namespace {

/**
 * Wraps a ConstBuffer<SocketAddress> in an interface for
 * PickFailover() and PickModulo().
 */
class AddressListWrapper {
	BalancerMap &balancer;
	FailureManager &failure_manager;
	const ConstBuffer<SocketAddress> list;

public:
	constexpr AddressListWrapper(BalancerMap &_balancer,
				     FailureManager &_failure_manager,
				     ConstBuffer<SocketAddress> _list) noexcept
		:balancer(_balancer),
		 failure_manager(_failure_manager), list(_list) {}

	gcc_pure
	auto &GetRoundRobinBalancer() const noexcept {
		return balancer.MakeRoundRobinBalancer(GetHashKey(list));
	}

	gcc_pure
	bool Check(const Expiry now, SocketAddress address,
		   bool allow_fade) const noexcept {
		return failure_manager.Check(now, address, allow_fade);
	}

	constexpr auto size() const noexcept {
		return list.size;
	}

	auto begin() const noexcept {
		return std::begin(list);
	}

	auto end() const noexcept {
		return std::end(list);
	}
};

}

SocketAddress
BalancerMap::Get(const Expiry now,
		 const AddressList &list, sticky_hash_t sticky_hash) noexcept
{
	return PickGeneric(now, list.sticky_mode,
			   AddressListWrapper(*this, failure_manager,
					      list.addresses),
			   sticky_hash);
}

inline RoundRobinBalancer &
BalancerMap::MakeRoundRobinBalancer(HashKey key) noexcept
{
	auto *item = cache.Get(key);

	if (item == nullptr)
		/* create a new cache item */
		item = &cache.Put(std::move(key), RoundRobinBalancer());

	return *item;
}
