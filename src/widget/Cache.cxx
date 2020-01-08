/*
 * Copyright 2007-2019 Content Management AG
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

#include "Cache.hxx"
#include "pool/pool.hxx"
#include "AllocatorPtr.hxx"

inline
WidgetClassCache::Item::Item(PoolPtr &&_pool, const WidgetClass &_cls) noexcept
	:PoolHolder(std::move(_pool)), cls(AllocatorPtr(pool), _cls)
{
}

WidgetClassCache::WidgetClassCache(struct pool &parent_pool) noexcept
	:PoolHolder(pool_new_dummy(&parent_pool, "WidgetClassCache")) {}

void
WidgetClassCache::Put(const char *name, const WidgetClass &cls) noexcept
{
	auto item_pool = pool_new_linear(pool, "WidgetClassCacheItem", 4096);

	/* move the string to the new pool */
	name = p_strdup(item_pool, name);

	map.emplace(std::piecewise_construct,
		    std::forward_as_tuple(name),
		    std::forward_as_tuple(std::move(item_pool), cls));
}
