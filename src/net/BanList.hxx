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
		uint_least64_t operator()(const Item &item) const noexcept;
	};

	using Map = IntrusiveHashSet<Item, 4096,
				     IntrusiveHashSetOperators<Item,
							       GetKey,
							       std::hash<uint_least64_t>,
							       std::equal_to<uint_least64_t>>>;

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
	BanAction Get(std::string_view host) noexcept;

	/**
	 * Set a ban on a host.
	 */
	void Set(std::string_view host, BanAction action, Event::Duration duration) noexcept;

private:
	static constexpr uint_least64_t CalcHash(std::string_view host) noexcept;

	void OnCleanupTimer() noexcept;
	void ScheduleCleanup() noexcept;
};
