/*
 * Various utilities for working with HTTP objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_UTIL_H
#define __BENG_HTTP_UTIL_H

#include "strmap.h"

#include <stdbool.h>

struct pool;
struct strref;

/**
 * Splits a comma separated list into a string array.  The return
 * value is NULL terminated.
 */
char **
http_list_split(struct pool *pool, const char *p);

gcc_pure
bool
http_list_contains(const char *list, const char *item);

/**
 * Case-insensitive version of http_list_contains().
 */
gcc_pure
bool
http_list_contains_i(const char *list, const char *item);

gcc_pure
static inline int
http_client_accepts_encoding(struct strmap *request_headers,
                             const char *coding)
{
    const char *accept_encoding = strmap_get(request_headers, "accept-encoding");
    return accept_encoding != NULL &&
        http_list_contains(accept_encoding, coding);
}

struct strref *
http_header_param(struct strref *dest, const char *value, const char *name);

#endif
