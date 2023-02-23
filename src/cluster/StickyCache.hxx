// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#ifndef BENG_PROXY_STICKY_CACHE_HXX
#define BENG_PROXY_STICKY_CACHE_HXX

#include "StickyHash.hxx"
#include "util/Cache.hxx"

class StickyCache : public Cache<sticky_hash_t, std::string, 32768, 4093>
{
};

#endif
