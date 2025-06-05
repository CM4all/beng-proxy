// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#ifndef BENG_PROXY_STICKY_CACHE_HXX
#define BENG_PROXY_STICKY_CACHE_HXX

#include "StickyHash.hxx"
#include "util/StaticCache.hxx"

class StickyCache : public StaticCache<sticky_hash_t, std::string, 32768, 4093>
{
};

#endif
