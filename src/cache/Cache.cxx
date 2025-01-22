// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Cache.hxx"
#include "Handler.hxx"
#include "Item.hxx"
#include "event/Loop.hxx"

#include <cassert>

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
	assert(!item->IsAbandoned() || !item->IsRemoved());
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
Cache::RefreshItem(CacheItem &item) noexcept
{
	/* move to the front of the linked list */
	sorted_items.erase(sorted_items.iterator_to(item));
	sorted_items.push_back(item);
}

void
Cache::RemoveItem(CacheItem &item) noexcept
{
	assert(!item.IsRemoved());

	items.erase_and_dispose(items.iterator_to(item),
				ItemRemover(*this));
}

CacheItem *
Cache::Get(StringWithHash key) noexcept
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

	RefreshItem(*item);
	return item;
}

CacheItem *
Cache::GetMatch(StringWithHash key,
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
	RefreshItem(*i);
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
Cache::Add(CacheItem &item) noexcept
{
	/* XXX size constraints */
	if (!NeedRoom(item.size)) {
		item.Destroy();
		return false;
	}

	items.insert(item);
	sorted_items.push_back(item);

	size += item.size;

	if (handler != nullptr)
		handler->OnCacheItemAdded(item);

	cleanup_timer.Enable();
	return true;
}

bool
Cache::Put(CacheItem &item) noexcept
{
	/* XXX size constraints */

	assert(item.size > 0);
	assert(item.IsAbandoned());

	if (!NeedRoom(item.size)) {
		item.Destroy();
		return false;
	}

	auto i = items.find(item.GetKey());
	if (i != items.end())
		RemoveItem(*i);

	size += item.size;

	items.insert(item);
	sorted_items.push_back(item);

	if (handler != nullptr)
		handler->OnCacheItemAdded(item);

	cleanup_timer.Enable();
	return true;
}

bool
Cache::PutMatch(CacheItem &item,
		bool (*match)(const CacheItem *, void *), void *ctx) noexcept
{
	auto *old = GetMatch(item.GetKey(), match, ctx);

	assert(item.size > 0);
	assert(item.IsAbandoned());

	if (old != nullptr)
		RemoveItem(*old);

	return Add(item);
}

void
Cache::Remove(StringWithHash key) noexcept
{
	items.remove_and_dispose_key(key, ItemRemover{*this});
}

void
Cache::Remove(CacheItem &item) noexcept
{
	if (item.IsRemoved()) {
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
