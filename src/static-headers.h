/*
 * Handle the request/response headers for static files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STATIC_HEADERS_H
#define BENG_PROXY_STATIC_HEADERS_H

#include "pool.h"

#include <sys/types.h>

struct strmap;
struct stat;

enum range_type {
    RANGE_NONE,
    RANGE_VALID,
    RANGE_INVALID
};

struct file_request {
    enum range_type range;

    off_t skip;
    off_t size;
};

void
static_response_headers(pool_t pool, struct strmap *headers,
                        int fd, const struct stat *st,
                        const char *content_type);

#endif
