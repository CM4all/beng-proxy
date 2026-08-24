// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/Chrono.hxx"
#include "event/FarTimerEvent.hxx"
#include "util/IntrusiveHashSet.hxx"
#include "util/IntrusiveList.hxx"

#include <cstdint>
#include <string_view>

class BareInetAddress;

enum class BanAction : uint_least8_t {
	NONE,
	REJECT,
	TARPIT,
};

/**
 * Manager for a list of banned hosts.
 */
class BanList {
	struct Item;

	struct GetKey {
		const BareInetAddress &operator()(const Item &item) const noexcept;
	};

	struct Hash {
		uint_least64_t operator()(const BareInetAddress &address) const noexcept;
	};

	using Map = IntrusiveHashSet<Item, 4096,
				     IntrusiveHashSetOperators<Item,
							       GetKey, Hash,
							       std::equal_to<BareInetAddress>>>;

	Map map;

	FarTimerEvent cleanup_timer;

public:
	BanList(EventLoop &event_loop) noexcept;
	~BanList() noexcept;

	auto &GetEventLoop() const noexcept {
		return cleanup_timer.GetEventLoop();
	}

	void BeginShutdown() noexcept {
		cleanup_timer.Cancel();
	}

	/**
	 * Check whether a host is banned.
	 */
	[[gnu::pure]]
	BanAction Get(const BareInetAddress &address) noexcept;

	/**
	 * Set a ban on a host.
	 */
	void Set(const BareInetAddress &address, BanAction action, Event::Duration duration) noexcept;

private:
	void OnCleanupTimer() noexcept;
	void ScheduleCleanup() noexcept;
};
