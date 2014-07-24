/*
 * Calculate maximum cache item age.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_CACHE_AGE_HXX
#define BENG_PROXY_HTTP_CACHE_AGE_HXX

#include <inline/compiler.h>

#include <time.h>

struct http_cache_info;
struct strmap;

/**
 * Calculate the "expires" value for the new cache item, based on the
 * "Expires" response header.
 */
gcc_pure
time_t
http_cache_calc_expires(const struct http_cache_info *info,
                        const struct strmap *vary);

#endif
