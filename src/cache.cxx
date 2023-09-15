// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "cache.hxx"
#include "event/Loop.hxx"
#include "util/djb_hash.hxx"
#include "util/StringAPI.hxx"

#include <assert.h>
#include <string.h>

inline size_t
CacheItem::Hash::operator()(const char *_key) const noexcept
{
	assert(_key != nullptr);

	return djb_hash_string(_key);
}

inline bool
CacheItem::Equal::operator()(const char *a, const char *b) const noexcept
{
	assert(a != nullptr);
	assert(b != nullptr);

	return StringIsEqual(a, b);
}

void
CacheItem::Release() noexcept
{
	if (IsAbandoned())
		Destroy();
	else
		/* this item is locked - postpone the Destroy() call */
		removed = true;
}

Cache::Cache(EventLoop &event_loop,
	     size_t _max_size,
	     CacheHandler *_handler) noexcept
	:max_size(_max_size),
	 handler(_handler),
	 cleanup_timer(event_loop, std::chrono::minutes(1),
		       BIND_THIS_METHOD(ExpireCallback)) {}

Cache::~Cache() noexcept
{
	items.clear_and_dispose([this](CacheItem *item){
		assert(item->IsAbandoned());
		assert(size >= item->size);
		size -= item->size;

#ifndef NDEBUG
		sorted_items.erase(sorted_items.iterator_to(*item));
#endif

		item->Destroy();
	});

	assert(size == 0);
	assert(sorted_items.empty());
}

std::chrono::steady_clock::time_point
Cache::SteadyNow() const noexcept
{
	return GetEventLoop().SteadyNow();
}

std::chrono::system_clock::time_point
Cache::SystemNow() const noexcept
{
	return GetEventLoop().SystemNow();
}

void
Cache::ItemRemoved(CacheItem *item) noexcept
{
	assert(item != nullptr);
	assert(item->size > 0);
	assert(!item->IsAbandoned() || !item->removed);
	assert(size >= item->size);

	sorted_items.erase(sorted_items.iterator_to(*item));

	size -= item->size;

	if (handler != nullptr)
		handler->OnCacheItemRemoved(*item);

	item->Release();

	if (size == 0)
		cleanup_timer.Disable();
}

void
Cache::Flush() noexcept
{
	items.clear_and_dispose(Cache::ItemRemover(*this));
}

void
Cache::RefreshItem(CacheItem &item,
		   std::chrono::steady_clock::time_point now) noexcept
{
	item.last_accessed = now;

	/* move to the front of the linked list */
	sorted_items.erase(sorted_items.iterator_to(item));
	sorted_items.push_back(item);
}

void
Cache::RemoveItem(CacheItem &item) noexcept
{
	assert(!item.removed);

	items.erase_and_dispose(items.iterator_to(item),
				ItemRemover(*this));
}

CacheItem *
Cache::Get(const char *key) noexcept
{
	auto i = items.find(key);
	if (i == items.end())
		return nullptr;

	CacheItem *item = &*i;

	const auto now = SteadyNow();

	if (!item->Validate(now)) {
		RemoveItem(*item);
		return nullptr;
	}

	RefreshItem(*item, now);
	return item;
}

CacheItem *
Cache::GetMatch(const char *key,
		bool (*match)(const CacheItem *, void *),
		void *ctx) noexcept
{
	const auto now = SteadyNow();

	auto i = items.expire_find_if(key, [now](const auto &item){
		return !item.Validate(now);
	}, [this](auto *item){
		RemoveItem(*item);
	}, [match, ctx](const auto &item){
		return match(&item, ctx);
	});

	if (i == items.end())
		return nullptr;

	/* this one matches: return it to the caller */
	RefreshItem(*i, now);
	return &*i;
}

void
Cache::DestroyOldestItem() noexcept
{
	if (sorted_items.empty())
		return;

	CacheItem &item = sorted_items.front();
	RemoveItem(item);
}

bool
Cache::NeedRoom(size_t _size) noexcept
{
	if (_size > max_size)
		return false;

	while (true) {
		if (size + _size <= max_size)
			return true;

		DestroyOldestItem();
	}
}

bool
Cache::Add(const char *key, CacheItem &item) noexcept
{
	/* XXX size constraints */
	if (!NeedRoom(item.size)) {
		item.Destroy();
		return false;
	}

	item.key = key;
	items.insert(item);
	sorted_items.push_back(item);

	size += item.size;
	item.last_accessed = SteadyNow();

	if (handler != nullptr)
		handler->OnCacheItemAdded(item);

	cleanup_timer.Enable();
	return true;
}

bool
Cache::Put(const char *key, CacheItem &item) noexcept
{
	/* XXX size constraints */

	assert(item.size > 0);
	assert(item.IsAbandoned());
	assert(!item.removed);

	if (!NeedRoom(item.size)) {
		item.Destroy();
		return false;
	}

	item.key = key;

	auto i = items.find(key);
	if (i != items.end())
		RemoveItem(*i);

	size += item.size;
	item.last_accessed = SteadyNow();

	items.insert(item);
	sorted_items.push_back(item);

	if (handler != nullptr)
		handler->OnCacheItemAdded(item);

	cleanup_timer.Enable();
	return true;
}

bool
Cache::PutMatch(const char *key, CacheItem &item,
		bool (*match)(const CacheItem *, void *), void *ctx) noexcept
{
	auto *old = GetMatch(key, match, ctx);

	assert(item.size > 0);
	assert(item.IsAbandoned());
	assert(!item.removed);

	if (old != nullptr)
		RemoveItem(*old);

	return Add(key, item);
}

void
Cache::Remove(const char *key) noexcept
{
	items.remove_and_dispose_key(key, [this](CacheItem *item){
		ItemRemoved(item);
	});
}

void
Cache::RemoveMatch(const char *key,
		   bool (*match)(const CacheItem *, void *),
		   void *ctx) noexcept
{
	items.remove_and_dispose_key_if(key, [match, ctx](const CacheItem &item){
		return match(&item, ctx);
	}, [this](CacheItem *item){
		ItemRemoved(item);
	});
}

void
Cache::Remove(CacheItem &item) noexcept
{
	if (item.removed) {
		/* item has already been removed by somebody else */
		assert(!item.IsAbandoned());
		return;
	}

	RemoveItem(item);
}

unsigned
Cache::RemoveAllMatch(bool (*match)(const CacheItem *, void *),
		      void *ctx) noexcept
{
	unsigned removed = 0;

	for (auto i = sorted_items.begin(), end = sorted_items.end();
	     i != end;) {
		CacheItem &item = *i++;

		if (!match(&item, ctx))
			continue;

		items.erase(items.iterator_to(item));
		ItemRemoved(&item);
		++removed;
	}

	return removed;
}

static constexpr std::chrono::steady_clock::time_point
ToSteady(std::chrono::steady_clock::time_point steady_now,
	 std::chrono::system_clock::time_point system_now,
	 std::chrono::system_clock::time_point t) noexcept
{
	return t > system_now
		? steady_now + (t - system_now)
		: std::chrono::steady_clock::time_point();
}

CacheItem::CacheItem(std::chrono::steady_clock::time_point now,
		     std::chrono::system_clock::time_point system_now,
		     std::chrono::system_clock::time_point _expires,
		     size_t _size) noexcept
	:CacheItem(ToSteady(now, system_now, _expires), _size)
{
}

CacheItem::CacheItem(std::chrono::steady_clock::time_point now,
		     std::chrono::seconds max_age, size_t _size) noexcept
	:CacheItem(now + max_age, _size)
{
}

void
CacheItem::OnAbandoned() noexcept
{
	if (removed)
		/* postponed destroy */
		Destroy();
}

void
CacheItem::SetExpires(std::chrono::steady_clock::time_point steady_now,
		      std::chrono::system_clock::time_point system_now,
		      std::chrono::system_clock::time_point _expires) noexcept
{
	expires = ToSteady(steady_now, system_now, _expires);
}

/** clean up expired cache items every 60 seconds */
bool
Cache::ExpireCallback() noexcept
{
	const auto now = SteadyNow();

	for (auto i = sorted_items.begin(), end = sorted_items.end(); i != end;) {
		CacheItem &item = *i++;

		if (item.expires > now)
			/* not yet expired */
			continue;

		RemoveItem(item);
	}

	return size > 0;
}

void
Cache::EventAdd() noexcept
{
	if (size > 0)
		cleanup_timer.Enable();
}

void
Cache::EventDel() noexcept
{
	cleanup_timer.Disable();
}
