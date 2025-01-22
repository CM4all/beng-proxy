// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Item.hxx"
#include "util/djb_hash.hxx"
#include "util/StringAPI.hxx"

#include <cassert>

size_t
CacheItem::Hash::operator()(const char *_key) const noexcept
{
	assert(_key != nullptr);

	return djb_hash_string(_key);
}

bool
CacheItem::Equal::operator()(const char *a, const char *b) const noexcept
{
	assert(a != nullptr);
	assert(b != nullptr);

	return StringIsEqual(a, b);
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
	if (IsRemoved())
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
