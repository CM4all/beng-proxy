/*
 * Calculate maximum cache item age.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_CACHE_AGE_HXX
#define BENG_PROXY_HTTP_CACHE_AGE_HXX

#include <inline/compiler.h>

#include <chrono>

struct HttpCacheResponseInfo;
struct StringMap;

/**
 * Calculate the "expires" value for the new cache item, based on the
 * "Expires" response header.
 */
gcc_pure
std::chrono::system_clock::time_point
http_cache_calc_expires(const HttpCacheResponseInfo &info,
                        const StringMap &vary);

#endif
