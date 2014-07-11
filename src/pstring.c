/*
 * String allocation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pool.h"

#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

void *
p_memdup_impl(struct pool *pool, const void *src, size_t length
              TRACE_ARGS_DECL)
{
    void *dest = p_malloc_fwd(pool, length);
    memcpy(dest, src, length);
    return dest;
}

char *
p_strdup_impl(struct pool *pool, const char *src
              TRACE_ARGS_DECL)
{
    return p_memdup_fwd(pool, src, strlen(src) + 1);
}

char *
p_strndup_impl(struct pool *pool, const char *src, size_t length TRACE_ARGS_DECL)
{
    char *dest = p_malloc_fwd(pool, length + 1);
    memcpy(dest, src, length);
    dest[length] = 0;
    return dest;
}

char * gcc_malloc
p_sprintf(struct pool *pool, const char *fmt, ...)
{
    size_t length;
    int gcc_unused length2;
    va_list ap;
    char *p;

    va_start(ap, fmt);
    length = (size_t)vsnprintf(NULL, 0, fmt, ap) + 1;
    va_end(ap);

    p = p_malloc(pool, length);

    va_start(ap, fmt);
    length2 = vsnprintf(p, length, fmt, ap);
    va_end(ap);

    assert((size_t)length2 + 1 == length);

    return p;
}

char * gcc_malloc
p_strcat(struct pool *pool, const char *first, ...)
{
    size_t length = 1;
    va_list ap;
    const char *s;
    char *ret, *p;

    va_start(ap, first);
    for (s = first; s != NULL; s = va_arg(ap, const char*))
        length += strlen(s);
    va_end(ap);

    ret = p = p_malloc(pool, length);

    va_start(ap, first);
    for (s = first; s != NULL; s = va_arg(ap, const char*)) {
        length = strlen(s);
        memcpy(p, s, length);
        p += length;
    }
    va_end(ap);

    *p = 0;

    return ret;
}

char * gcc_malloc
p_strncat(struct pool *pool, const char *first, size_t first_length, ...)
{
    size_t length = first_length + 1;
    va_list ap;
    const char *s;
    char *ret, *p;

    va_start(ap, first_length);
    for (s = va_arg(ap, const char*); s != NULL; s = va_arg(ap, const char*))
        length += va_arg(ap, size_t);
    va_end(ap);

    ret = p = p_malloc(pool, length);

    memcpy(p, first, first_length);
    p += first_length;

    va_start(ap, first_length);
    for (s = va_arg(ap, const char*); s != NULL; s = va_arg(ap, const char*)) {
        length = va_arg(ap, size_t);
        memcpy(p, s, length);
        p += length;
    }
    va_end(ap);

    *p = 0;

    return ret;
}
