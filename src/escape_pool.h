/*
 * Escape classes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ESCAPE_POOL_H
#define BENG_PROXY_ESCAPE_POOL_H

#include <stddef.h>

struct pool;
struct escape_class;

#ifdef __cplusplus
extern "C" {
#endif

char *
escape_dup(struct pool *pool, const struct escape_class *cls,
           const char *p, size_t length);

#ifdef __cplusplus
}
#endif

#endif
