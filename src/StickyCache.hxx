/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STICKY_CACHE_HXX
#define BENG_PROXY_STICKY_CACHE_HXX

#include "util/Cache.hxx"

#include <stdint.h>

class StickyCache : public Cache<uint32_t, std::string, 32768, 4093>
{
};

#endif
