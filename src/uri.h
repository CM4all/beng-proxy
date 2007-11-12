/*
 * Functions for working with URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URI_H
#define __BENG_URI_H

#include "strref.h"
#include "pool.h"

struct parsed_uri {
    struct strref base;
    struct strref args;
    struct strref query;
};

int
uri_parse(pool_t pool, struct parsed_uri *dest, const char *src);

const char *
uri_absolute(pool_t pool, const char *base, const char *uri, size_t length);

#endif
