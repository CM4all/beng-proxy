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

#pragma once

#include "RoundRobinBalancer.hxx"
#include "StickyHash.hxx"
#include "HashKey.hxx"
#include "util/Cache.hxx"

struct AddressList;
class SocketAddress;
class Expiry;

/**
 * Load balancer for AddressList.
 */
class BalancerMap {
	Cache<HashKey, RoundRobinBalancer, 2048, 1021> cache;

public:
	RoundRobinBalancer &MakeRoundRobinBalancer(HashKey key) noexcept;

	/**
	 * Wrap the given "base" address list wrapper in one which
	 * implements GetRoundRobinBalancer() and can thus be passed
	 * to PickGeneric().
	 */
	template<typename Base>
	auto MakeAddressListWrapper(Base &&base) noexcept {
		return Wrapper<Base>(std::move(base), *this);
	}

	template<typename Base>
	class Wrapper : public Base {
		BalancerMap &balancer;

	public:
		Wrapper(Base &&base, BalancerMap &_balancer) noexcept
			:Base(std::move(base)), balancer(_balancer) {}

		gcc_pure
		auto &GetRoundRobinBalancer() const noexcept {
			return balancer.MakeRoundRobinBalancer(GetHashKey(*this));
		}
	};
};
