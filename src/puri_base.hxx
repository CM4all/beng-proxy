/*
 * Functions for working with base URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PURI_BASE_HXX
#define BENG_PROXY_PURI_BASE_HXX

#include <inline/compiler.h>

#include <stddef.h>

struct pool;

/**
 * @return (size_t)-1 on mismatch
 */
gcc_pure
size_t
base_string_unescape(struct pool *pool, const char *p, const char *tail);

#endif
