/*
 * Static file support for resource_get().
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_request.hxx"
#include "static-headers.h"
#include "http_response.hxx"
#include "gerrno.h"
#include "strmap.h"
#include "istream.h"
#include "istream_file.h"
#include "pool.h"

#include <http/status.h>

#include <assert.h>
#include <sys/stat.h>

void
static_file_get(struct pool *pool, const char *path, const char *content_type,
                const struct http_response_handler *handler,
                void *handler_ctx)
{
    assert(path != nullptr);

    struct stat st;
    if (lstat(path, &st) != 0) {
        GError *error = new_error_errno();
        g_prefix_error(&error, "Failed to open %s: ", path);
        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    if (!S_ISREG(st.st_mode) && !S_ISCHR(st.st_mode)) {
        struct http_response_handler_ref handler_ref;
        http_response_handler_set(&handler_ref, handler, handler_ctx);
        http_response_handler_invoke_message(&handler_ref, pool,
                                             HTTP_STATUS_NOT_FOUND,
                                             "Not a regular file");
        return;
    }

    const off_t size = S_ISCHR(st.st_mode)
        ? -1 : st.st_size;

    GError *error = nullptr;
    struct istream *body = istream_file_new(pool, path, size, &error);
    if (body == nullptr) {
        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    struct strmap *headers = strmap_new(pool, 16);
    static_response_headers(pool, headers,
                            istream_file_fd(body), &st,
                            content_type);

    http_response_handler_direct_response(handler, handler_ctx,
                                          HTTP_STATUS_OK,
                                          headers, body);
}
