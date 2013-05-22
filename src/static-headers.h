/*
 * Handle the request/response headers for static files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STATIC_HEADERS_H
#define BENG_PROXY_STATIC_HEADERS_H

#include <sys/types.h>

struct pool;
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

/**
 * @param fd a file descriptor for loading xattr, or -1 to disable
 * xattr
 */
void
static_response_headers(struct pool *pool, struct strmap *headers,
                        int fd, const struct stat *st,
                        const char *content_type);

#endif
