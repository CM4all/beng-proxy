/*
 * Escape classes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ESCAPE_POOL_HXX
#define BENG_PROXY_ESCAPE_POOL_HXX

#include <stddef.h>

struct pool;
struct escape_class;

char *
escape_dup(struct pool *pool, const struct escape_class *cls,
           const char *p, size_t length);

#endif
