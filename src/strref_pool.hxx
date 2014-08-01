/*
 * Allocating strref data from a memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STRREF_POOL_HXX
#define BENG_PROXY_STRREF_POOL_HXX

#include "strref.h"
#include "pool.h"

#include <inline/poison.h>

static gcc_always_inline char *
strref_set_dup_impl(struct pool *pool, struct strref *dest,
                    const struct strref *src
                    TRACE_ARGS_DECL)
{
    assert(dest != nullptr);
    assert(src != nullptr);
    assert(src->length == 0 || src->data != nullptr);

    if (src->length == 0) {
        dest->length = 0;
        return nullptr;
    } else {
        dest->length = src->length;
        char *p = (char *)p_memdup_fwd(pool, src->data, src->length);
        dest->data = p;
        return p;
    }
}

#define strref_set_dup(pool, dest, src) strref_set_dup_impl(pool, dest, src TRACE_ARGS)
#define strref_set_dup_fwd(pool, src, length) strref_set_dup_impl(pool, dest, src TRACE_ARGS_FWD)

static gcc_always_inline void
strref_append_impl(struct pool *pool, struct strref *dest,
                   const struct strref *src
                   TRACE_ARGS_DECL)
{
    char *p;

    assert(dest != nullptr);
    assert(src != nullptr);
    assert(src->length == 0 || src->data != nullptr);

    if (src->length == 0)
        return;

    p = (char *)p_malloc_fwd(pool, dest->length + src->length);
    memcpy(p, dest->data, dest->length);
    memcpy(p + dest->length, src->data, src->length);

    dest->data = p;
    dest->length += src->length;
}

#define strref_append(pool, dest, src) strref_append_impl(pool, dest, src TRACE_ARGS)
#define strref_append_fwd(pool, src, length) strref_append_impl(pool, dest, src TRACE_ARGS_FWD)

static gcc_always_inline char *
strref_dup_impl(struct pool *pool, const struct strref *s
           TRACE_ARGS_DECL)
{
    assert(pool != nullptr);
    assert(s != nullptr);

    return p_strndup_fwd(pool, s->data, s->length);
}

#define strref_dup(pool, s) strref_dup_impl(pool, s TRACE_ARGS)
#define strref_dup_fwd(pool, s) strref_dup_impl(pool, s TRACE_ARGS_FWD)

static gcc_always_inline void
strref_free(struct pool *pool, struct strref *s)
{
    assert(pool != nullptr);
    assert(s != nullptr);
    assert(s->length > 0);
    assert(s->data != nullptr);

    p_free(pool, s->data);
    poison_undefined(s, sizeof(*s));
}

#endif
