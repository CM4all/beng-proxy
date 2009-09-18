/*
 * Handle the request/response headers for static files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "static-headers.h"
#include "strmap.h"
#include "format.h"
#include "date.h"

#include <assert.h>
#include <sys/stat.h>

#ifndef NO_XATTR
#include <attr/xattr.h>
#endif

static void
make_etag(char *p, const struct stat *st)
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

void
static_response_headers(pool_t pool, struct strmap *headers,
                        int fd, const struct stat *st,
                        const char *content_type)
{
    char buffer[256];

    if (content_type == NULL) {
#ifndef NO_XATTR
        ssize_t nbytes;

        nbytes = fgetxattr(fd, "user.Content-Type",
                           buffer, sizeof(buffer) - 1);
        if (nbytes > 0) {
            assert((size_t)nbytes < sizeof(buffer));
            buffer[nbytes] = 0;
            content_type = p_strdup(pool, buffer);
        } else {
#endif /* #ifndef NO_XATTR */
            content_type = "application/octet-stream";
#ifndef NO_XATTR
        }
#endif /* #ifndef NO_XATTR */
    }

    strmap_add(headers, "content-type", content_type);

    strmap_add(headers, "last-modified",
               p_strdup(pool, http_date_format(st->st_mtime)));

    make_etag(buffer, st);
    strmap_add(headers, "etag", p_strdup(pool, buffer));
}
