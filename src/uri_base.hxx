/*
 * Functions for working with base URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URI_BASE_HXX
#define BENG_PROXY_URI_BASE_HXX

#include <inline/compiler.h>

#include <stddef.h>

struct pool;

/**
 * Calculate the URI tail after a base URI from a request URI.
 * Returns nullptr if no such tail URI is possible (e.g. if the
 * specified URI is not "within" the base, or if there is no base at
 * all).
 *
 * @param uri the URI specified by the tcache client, may be nullptr
 * @param base the base URI, as given by the translation server,
 * stored in the cache item, may be nullptr
 */
gcc_pure
const char *
base_tail(const char *uri, const char *base);

/**
 * Similar to base_tail(), but assert that there is a base match.
 */
gcc_pure
const char *
require_base_tail(const char *uri, const char *base);

/**
 * Determine the length of the base prefix in the given string.
 *
 * @return (size_t)-1 on mismatch
 */
gcc_pure
size_t
base_string(const char *p, const char *tail);

/**
 * @return (size_t)-1 on mismatch
 */
gcc_pure
size_t
base_string_unescape(struct pool *pool, const char *p, const char *tail);

#endif
