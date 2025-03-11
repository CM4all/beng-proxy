// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "RoundRobinBalancer.hxx"
#include "PickGeneric.hxx"
#include "StickyHash.hxx"
#include "HashKey.hxx"
#include "time/Expiry.hxx"
#include "util/StaticCache.hxx"

/**
 * Load balancer for AddressList.
 */
class BalancerMap {
	StaticCache<HashKey, RoundRobinBalancer, 2048, 1021> cache;

public:
	RoundRobinBalancer &MakeRoundRobinBalancer(HashKey key) noexcept;

	/**
	 * Wrap the given "base" address list wrapper in one which
	 * implements GetRoundRobinBalancer() and can thus be passed
	 * to PickGeneric().
	 */
	template<typename Base>
	auto MakeAddressListWrapper(Base &&base,
				    StickyMode sticky_mode) noexcept {
		return Wrapper<Base>(std::move(base), *this, sticky_mode);
	}

	template<typename Base>
	class Wrapper : public Base {
		BalancerMap &balancer;

		const StickyMode sticky_mode;

	public:
		Wrapper(Base &&base, BalancerMap &_balancer,
			StickyMode _sticky_mode) noexcept
			:Base(std::move(base)), balancer(_balancer),
			 sticky_mode(_sticky_mode) {}

		[[gnu::pure]]
		auto &GetRoundRobinBalancer() const noexcept {
			return balancer.MakeRoundRobinBalancer(GetHashKey(*this));
		}

		auto Pick(Expiry now, sticky_hash_t sticky_hash) const noexcept {
			return PickGeneric(now, sticky_mode, *this, sticky_hash);
		}
	};
};
