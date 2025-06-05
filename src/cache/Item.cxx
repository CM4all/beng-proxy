// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Item.hxx"

static constexpr std::chrono::steady_clock::time_point
ToSteady(std::chrono::steady_clock::time_point steady_now,
	 std::chrono::system_clock::time_point system_now,
	 std::chrono::system_clock::time_point t) noexcept
{
	return t > system_now
		? steady_now + (t - system_now)
		: std::chrono::steady_clock::time_point();
}

CacheItem::CacheItem(StringWithHash _key, std::size_t _size,
				       std::chrono::steady_clock::time_point now,
		     std::chrono::system_clock::time_point system_now,
		     std::chrono::system_clock::time_point _expires) noexcept
	:CacheItem(_key, _size, ToSteady(now, system_now, _expires))
{
}

CacheItem::CacheItem(StringWithHash _key, std::size_t _size,
		     std::chrono::steady_clock::time_point now,
		     std::chrono::seconds max_age) noexcept
	:CacheItem(_key, _size, now + max_age)
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
