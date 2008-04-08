/*
 * Various utilities for working with HTTP objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_UTIL_H
#define __BENG_HTTP_UTIL_H

#include "strmap.h"

struct strref;

int
http_list_contains(const char *list, const char *item);

static inline int
http_client_accepts_encoding(strmap_t request_headers,
                             const char *coding)
{
    const char *accept_encoding = strmap_get(request_headers, "accept-encoding");
    return accept_encoding != NULL &&
        http_list_contains(accept_encoding, coding);
}

struct strref *
http_header_param(struct strref *dest, const char *value, const char *name);

#endif
