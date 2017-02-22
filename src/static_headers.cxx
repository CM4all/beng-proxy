/*
 * Handle the request/response headers for static files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "static_headers.hxx"
#include "strmap.hxx"
#include "format.h"
#include "http_date.hxx"
#include "pool.hxx"

#include <attr/xattr.h>

#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>

void
static_etag(char *p, const struct stat &st)
{
    *p++ = '"';

    p += format_uint32_hex(p, (uint32_t)st.st_dev);

    *p++ = '-';

    p += format_uint32_hex(p, (uint32_t)st.st_ino);

    *p++ = '-';

    p += format_uint32_hex(p, (uint32_t)st.st_mtime);

    *p++ = '"';
    *p = 0;
}

bool
load_xattr_content_type(char *buffer, size_t size, int fd)
{
    if (fd < 0)
        return false;

    ssize_t nbytes = fgetxattr(fd, "user.Content-Type",
                               buffer, size - 1);
    if (nbytes <= 0)
        return false;

    assert((size_t)nbytes < size);
    buffer[nbytes] = 0;
    return true;
}

StringMap
static_response_headers(struct pool &pool,
                        int fd, const struct stat &st,
                        const char *content_type)
{
    StringMap headers(pool);

    if (S_ISCHR(st.st_mode))
        return headers;

    char buffer[256];

    if (content_type == nullptr)
        content_type = load_xattr_content_type(buffer, sizeof(buffer), fd)
            ? p_strdup(&pool, buffer)
            : "application/octet-stream";

    headers.Add("content-type", content_type);

    headers.Add("last-modified",
                p_strdup(&pool, http_date_format(std::chrono::system_clock::from_time_t(st.st_mtime))));

    static_etag(buffer, st);
    headers.Add("etag", p_strdup(&pool, buffer));

    return headers;
}
