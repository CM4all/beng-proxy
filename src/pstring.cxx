/*
 * String allocation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pool.hxx"
#include "util/CharUtil.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

static char *
Copy(char *dest, const char *src, size_t n)
{
    return (char *)mempcpy(dest, src, n);
}

static char *
CopyLower(char *dest, const char *src, size_t n)
{
    return std::transform(src, src + n, dest, ToLowerASCII);
}

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
    return (char *)p_memdup_fwd(pool, src, strlen(src) + 1);
}

char *
p_strdup_lower_impl(struct pool *pool, const char *src
                    TRACE_ARGS_DECL)
{
    return p_strndup_lower_fwd(pool, src, strlen(src));
}

char *
p_strndup_impl(struct pool *pool, const char *src, size_t length TRACE_ARGS_DECL)
{
    char *dest = (char *)p_malloc_fwd(pool, length + 1);
    *Copy(dest, src, length) = 0;
    return dest;
}

char *
p_strndup_lower_impl(struct pool *pool, const char *src, size_t length
                     TRACE_ARGS_DECL)
{
    char *dest = (char *)p_malloc_fwd(pool, length + 1);
    *CopyLower(dest, src, length) = 0;
    return dest;
}

char * gcc_malloc
p_sprintf(struct pool *pool, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t length = (size_t)vsnprintf(nullptr, 0, fmt, ap) + 1;
    va_end(ap);

    char *p = (char *)p_malloc(pool, length);

    va_start(ap, fmt);
    gcc_unused int length2 = vsnprintf(p, length, fmt, ap);
    va_end(ap);

    assert((size_t)length2 + 1 == length);

    return p;
}

char * gcc_malloc
p_strcat(struct pool *pool, const char *first, ...)
{
    va_list ap;

    size_t length = 1;
    va_start(ap, first);
    for (const char *s = first; s != nullptr; s = va_arg(ap, const char *))
        length += strlen(s);
    va_end(ap);

    char *result = (char *)p_malloc(pool, length);

    va_start(ap, first);
    char *p = result;
    for (const char *s = first; s != nullptr; s = va_arg(ap, const char *))
        p = Copy(p, s, strlen(s));
    va_end(ap);

    *p = 0;

    return result;
}

char * gcc_malloc
p_strncat(struct pool *pool, const char *first, size_t first_length, ...)
{
    va_list ap;

    size_t length = first_length + 1;
    va_start(ap, first_length);
    for (const char *s = va_arg(ap, const char *); s != nullptr;
         s = va_arg(ap, const char *))
        length += va_arg(ap, size_t);
    va_end(ap);

    char *result = (char *)p_malloc(pool, length);

    char *p = result;
    p = Copy(p, first, first_length);

    va_start(ap, first_length);
    for (const char *s = va_arg(ap, const char *); s != nullptr;
         s = va_arg(ap, const char *))
        p = Copy(p, s, va_arg(ap, size_t));
    va_end(ap);

    *p = 0;

    return result;
}
