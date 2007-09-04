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

char *
p_strdup(pool_t pool, const char *src)
{
    size_t length = strlen(src) + 1;
    char *dest = p_malloc(pool, length);
    memcpy(dest, src, length);
    return dest;
}

char *
p_strndup(pool_t pool, const char *src, size_t length)
{
    char *dest = p_malloc(pool, length + 1);
    memcpy(dest, src, length);
    dest[length] = 0;
    return dest;
}

char * attr_malloc
p_sprintf(pool_t pool, const char *fmt, ...)
{
#if __STDC_VERSION__ >= 199901L
    size_t length;
    int length2;
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
#else
#error C99 required for snprintf(NULL, 0, ...)
#endif
}

char * attr_malloc
p_strcat(pool_t pool, const char *first, ...)
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

char * attr_malloc
p_strncat(pool_t pool, const char *first, size_t first_length, ...)
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
