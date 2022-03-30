/*
 * Copyright 2022 CM4all GmbH
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

#include "event/FarTimerEvent.hxx"

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>

#include <cstdint>

class SocketAddress;
class PerClientAccounting;
class ClientAccountingMap;

class AccountedClientConnection {
	friend class PerClientAccounting;

	using SiblingsHook = boost::intrusive::list_member_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>;
	SiblingsHook siblings;

	PerClientAccounting *per_client = nullptr;

public:
	AccountedClientConnection() = default;
	~AccountedClientConnection() noexcept;

	AccountedClientConnection(const AccountedClientConnection &) = delete;
	AccountedClientConnection &operator=(const AccountedClientConnection &) = delete;
};

class PerClientAccounting final
	: public boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>
{
	friend class ClientAccountingMap;

	ClientAccountingMap &map;

	const uint_least64_t address;

	struct Hash {
		constexpr std::size_t operator()(const PerClientAccounting &value) const noexcept {
			return value.address;
		}

		constexpr std::size_t operator()(uint_least64_t address) const noexcept {
			return address;
		}
	};

	struct Equal {
		constexpr std::size_t operator()(uint_least64_t address, const PerClientAccounting &value) const noexcept {
			return address == value.address;
		}
	};

	using ConnectionList =
		boost::intrusive::list<AccountedClientConnection,
				       boost::intrusive::member_hook<AccountedClientConnection,
								     AccountedClientConnection::SiblingsHook,
								     &AccountedClientConnection::siblings>,
				       boost::intrusive::constant_time_size<true>>;

	ConnectionList connections;

	Event::TimePoint expires;

public:
	PerClientAccounting(ClientAccountingMap &_map, uint_least64_t _address) noexcept;

	[[gnu::pure]]
	bool Check() const noexcept;

	void AddConnection(AccountedClientConnection &c) noexcept;
	void RemoveConnection(AccountedClientConnection &c) noexcept;
};

class ClientAccountingMap {
	const std::size_t max_connections;

	using Map =
		boost::intrusive::unordered_set<PerClientAccounting,
						boost::intrusive::hash<PerClientAccounting::Hash>,
						boost::intrusive::equal<PerClientAccounting::Equal>,
						boost::intrusive::constant_time_size<false>>;

	static constexpr size_t N_BUCKETS = 3779;
	Map::bucket_type buckets[N_BUCKETS];

	Map map{Map::bucket_traits{buckets, N_BUCKETS}};

	FarTimerEvent cleanup_timer;

public:
	ClientAccountingMap(EventLoop &event_loop, std::size_t _max_connections) noexcept
		:max_connections(_max_connections),
		 cleanup_timer(event_loop, BIND_THIS_METHOD(OnCleanupTimer)) {}

	auto &GetEventLoop() const noexcept {
		return cleanup_timer.GetEventLoop();
	}

	std::size_t GetMaxConnections() const noexcept {
		return max_connections;
	}

	PerClientAccounting *Get(SocketAddress address) noexcept;

	void ScheduleCleanup() noexcept;

private:
	void OnCleanupTimer() noexcept;
};
