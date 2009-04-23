/*
 * Static file support for resource_get().
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "static-file.h"
#include "http.h"
#include "http-response.h"
#include "strmap.h"
#include "format.h"
#include "date.h"

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>

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
static_file_get(pool_t pool, const char *path, const char *content_type,
                const struct http_response_handler *handler,
                void *handler_ctx)
{
    int ret;
    struct stat st;
    off_t size;
    istream_t body;
    struct strmap *headers;
    char buffer[64];

    assert(path != NULL);

    ret = lstat(path, &st);
    if (ret != 0) {
        if (errno == ENOENT)
            http_response_handler_direct_response(handler, handler_ctx,
                                                  HTTP_STATUS_NOT_FOUND,
                                                  NULL,
                                                  istream_string_new(pool, "The requested file does not exist."));
        else
            http_response_handler_direct_abort(handler, handler_ctx);
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        http_response_handler_direct_response(handler, handler_ctx,
                                              HTTP_STATUS_NOT_FOUND,
                                              NULL,
                                              istream_string_new(pool, "Not a regular file"));
        return;
    }

    size = st.st_size;

    body = istream_file_new(pool, path, size);
    if (body == NULL) {
        if (errno == ENOENT)
            http_response_handler_direct_response(handler, handler_ctx,
                                                  HTTP_STATUS_NOT_FOUND,
                                                  NULL,
                                                  istream_string_new(pool, "The requested file does not exist."));
        else
            http_response_handler_direct_abort(handler, handler_ctx);
        return;
    }

    headers = strmap_new(pool, 16);

    if (content_type == NULL) {
#ifndef NO_XATTR
        ssize_t nbytes;
        char content_type_buffer[256];

        nbytes = fgetxattr(istream_file_fd(body), "user.Content-Type",
                           content_type_buffer,
                           sizeof(content_type_buffer) - 1);
        if (nbytes > 0) {
            assert((size_t)nbytes < sizeof(content_type_buffer));
            content_type_buffer[nbytes] = 0;
            content_type = p_strdup(pool, content_type_buffer);
        } else {
#endif /* #ifndef NO_XATTR */
            content_type = "application/octet-stream";
#ifndef NO_XATTR
        }
#endif /* #ifndef NO_XATTR */
    }

    strmap_add(headers, "content-type", content_type);

    strmap_add(headers, "last-modified", p_strdup(pool, http_date_format(st.st_mtime)));

    make_etag(buffer, &st);
    strmap_add(headers, "etag", p_strdup(pool, buffer));

    http_response_handler_direct_response(handler, handler_ctx,
                                          HTTP_STATUS_OK,
                                          headers, body);
}
