// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class CacheItem;

class CacheHandler {
public:
	virtual void OnCacheItemAdded(const CacheItem &item) noexcept = 0;
	virtual void OnCacheItemRemoved(const CacheItem &item) noexcept = 0;
};
