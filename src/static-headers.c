/*
 * Handle the request/response headers for static files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "static-headers.h"
#include "strmap.h"
#include "format.h"
#include "date.h"
#include "pool.h"

#include <assert.h>
#include <sys/stat.h>

#ifndef HAVE_ATTR_XATTR_H
#define NO_XATTR 1
#endif

#ifndef NO_XATTR
#include <attr/xattr.h>
#endif

void
static_etag(char *p, const struct stat *st)
{
    *p++ = '"';

    p += format_uint32_hex(p, (uint32_t)st->st_dev);

    *p++ = '-';

    p += format_uint32_hex(p, (uint32_t)st->st_ino);

    *p++ = '-';

    p += format_uint32_hex(p, (uint32_t)st->st_mtime);

    *p++ = '"';
    *p = 0;
}

static bool
load_xattr_content_type(char *buffer, size_t size, int fd)
{
#ifdef NO_XATTR
    (void)buffer;
    (void)size;
    (void)fd;

    return false;
#else
    if (fd < 0)
        return false;

    ssize_t nbytes = fgetxattr(fd, "user.Content-Type",
                               buffer, size - 1);
    if (nbytes <= 0)
        return false;

    assert((size_t)nbytes < size);
    buffer[nbytes] = 0;
    return true;
#endif
}

void
static_response_headers(struct pool *pool, struct strmap *headers,
                        int fd, const struct stat *st,
                        const char *content_type)
{
    char buffer[256];

    if (content_type == NULL)
        content_type = load_xattr_content_type(buffer, sizeof(buffer), fd)
            ? p_strdup(pool, buffer)
            : "application/octet-stream";

    strmap_add(headers, "content-type", content_type);

    strmap_add(headers, "last-modified",
               p_strdup(pool, http_date_format(st->st_mtime)));

    static_etag(buffer, st);
    strmap_add(headers, "etag", p_strdup(pool, buffer));
}
