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

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif
