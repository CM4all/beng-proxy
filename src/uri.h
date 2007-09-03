/*
 * Functions for working with URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URI_H
#define __BENG_URI_H

#include "pool.h"

struct parsed_uri {
    const char *base;
    size_t base_length;
    const char *args;
    size_t args_length;
    const char *query;
    size_t query_length;
};

void
uri_parse(struct parsed_uri *dest, const char *src);

const char *
uri_absolute(pool_t pool, const char *base, const char *uri, size_t length);

#endif
