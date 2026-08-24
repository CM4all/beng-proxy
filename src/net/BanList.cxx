// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "BanList.hxx"
#include "event/Loop.hxx"
#include "net/BareInetAddress.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/FNVHash.hxx"
#include "util/SpanCast.hxx"

struct BanList::Item : IntrusiveHashSetHook<> {
	const BareInetAddress address;

	BanAction action;

	Event::TimePoint expires;

	Item(const BareInetAddress &_address, BanAction _action, Event::TimePoint _expires) noexcept
		:address(_address), action(_action), expires(_expires) {}
};

inline const BareInetAddress &
BanList::GetKey::operator()(const Item &item) const noexcept
{
	return item.address;
}

inline uint_least64_t
BanList::Hash::operator()(const BareInetAddress &address) const noexcept
{
	return FNV1aHash64(ReferenceAsBytes(address));
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

inline auto
BanList::Find(Event::TimePoint now, const BareInetAddress &address) noexcept
{
	return map.expire_find_if(address, [now](const auto &item){
		return item.expires <= now;
	}, DeleteDisposer{}, [](const auto &){
		return true;
	});
}

BanAction
BanList::Get(const BareInetAddress &address) noexcept
{
	const auto now = GetEventLoop().SteadyNow();

	auto i = Find(now, address);
	if (i == map.end())
		return BanAction::NONE;

	return i->action;
}

void
BanList::Set(const BareInetAddress &address, BanAction action, Event::Duration duration) noexcept
{
	const auto now = GetEventLoop().SteadyNow();
	const auto expires = now + duration;

	auto [it, inserted] = map.insert_check(address);
	if (inserted) {
		if (duration <= Event::Duration::zero())
			/* no item exists, that's fine */
			return;

		auto *item = new Item(address, action, expires);
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

