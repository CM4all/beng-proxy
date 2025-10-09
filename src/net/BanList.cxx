// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "BanList.hxx"
#include "event/Loop.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/FNVHash.hxx"
#include "util/SpanCast.hxx"

struct BanList::Item : IntrusiveHashSetHook<> {
	/**
	 * We store only the hash, not the whole host name, for
	 * performance reasons.  Let's see if we can get away with
	 * this - this may overblock due to hash collisions.
	 */
	const uint_least64_t hash;

	BanAction action;

	Event::TimePoint expires;

	Item(uint_least64_t _hash, BanAction _action, Event::TimePoint _expires) noexcept
		:hash(_hash), action(_action), expires(_expires) {}
};

inline uint_least64_t
BanList::GetKey::operator()(const Item &item) const noexcept
{
	return item.hash;
}

BanList::BanList(EventLoop &event_loop) noexcept
	:cleanup_timer(event_loop, BIND_THIS_METHOD(OnCleanupTimer))
{
	ScheduleCleanup();
}

BanList::~BanList() noexcept
{
	map.clear_and_dispose(DeleteDisposer{});
}

constexpr uint_least64_t
BanList::CalcHash(std::string_view host) noexcept
{
	return FNV1aHash64(AsBytes(host));
}

BanAction
BanList::Get(std::string_view host) noexcept
{
	const auto hash = CalcHash(host);
	const auto now = GetEventLoop().SteadyNow();

	auto i = map.expire_find_if(hash, [now](const auto &item){
		return item.expires <= now;
	}, DeleteDisposer{}, [](const auto &){
		return true;
	});

	if (i == map.end())
		return BanAction::NONE;

	return i->action;
}

void
BanList::Set(std::string_view host, BanAction action, Event::Duration duration) noexcept
{
	const auto hash = CalcHash(host);
	const auto now = GetEventLoop().SteadyNow();
	const auto expires = now + duration;

	auto [it, inserted] = map.insert_check(hash);
	if (inserted) {
		if (duration <= Event::Duration::zero())
			/* no item exists, that's fine */
			return;

		auto *item = new Item(hash, action, expires);
		it = map.insert_commit(it, *item);
	} else {
		if (duration <= Event::Duration::zero()) {
			/* an item exists: remove it */
			map.erase_and_dispose(it, DeleteDisposer{});
			return;
		}

		it->action = action;
		it->expires = expires;
	}
}

void
BanList::OnCleanupTimer() noexcept
{
	ScheduleCleanup();
}

inline void
BanList::ScheduleCleanup() noexcept
{
	const auto now = GetEventLoop().SteadyNow();

	map.remove_and_dispose_if([now](const auto &item){
		return item.expires <= now;
	}, DeleteDisposer{});

	cleanup_timer.Schedule(std::chrono::minutes{10});
}

