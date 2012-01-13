/*
 * Functions for working with base URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URI_BASE_H
#define BENG_PROXY_URI_BASE_H

#include <inline/compiler.h>

#include <stddef.h>

struct pool;

gcc_pure
size_t
base_string(const char *p, const char *suffix);

gcc_pure
size_t
base_string_unescape(struct pool *pool, const char *p, const char *suffix);

#endif
