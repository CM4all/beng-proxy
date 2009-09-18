/*
 * Static file support for resource_get().
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "static-file.h"
#include "static-headers.h"
#include "http.h"
#include "http-response.h"
#include "strmap.h"

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>

static void
send_errno(pool_t pool,
             const struct http_response_handler *handler, void *handler_ctx)
{
    if (errno == ENOENT)
        http_response_handler_direct_response(handler, handler_ctx,
                                              HTTP_STATUS_NOT_FOUND,
                                              NULL,
                                              istream_string_new(pool, "The requested file does not exist."));
    else
        http_response_handler_direct_abort(handler, handler_ctx);
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

    assert(path != NULL);

    ret = lstat(path, &st);
    if (ret != 0) {
        send_errno(pool, handler, handler_ctx);
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
        send_errno(pool, handler, handler_ctx);
        return;
    }

    headers = strmap_new(pool, 16);
    static_response_headers(pool, headers,
                            istream_file_fd(body), &st,
                            content_type);

    http_response_handler_direct_response(handler, handler_ctx,
                                          HTTP_STATUS_OK,
                                          headers, body);
}
