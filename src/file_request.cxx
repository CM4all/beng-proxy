/*
 * Static file support for resource_get().
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_request.hxx"
#include "static_headers.hxx"
#include "http_response.hxx"
#include "gerrno.h"
#include "strmap.hxx"
#include "istream.h"
#include "istream_file.hxx"
#include "pool.hxx"

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
        handler->InvokeAbort(handler_ctx, error);
        return;
    }

    if (!S_ISREG(st.st_mode) && !S_ISCHR(st.st_mode)) {
        handler->InvokeMessage(handler_ctx, *pool, HTTP_STATUS_NOT_FOUND,
                               "Not a regular file");
        return;
    }

    const off_t size = S_ISCHR(st.st_mode)
        ? -1 : st.st_size;

    GError *error = nullptr;
    struct istream *body = istream_file_new(pool, path, size, &error);
    if (body == nullptr) {
        handler->InvokeAbort(handler_ctx, error);
        return;
    }

    struct strmap *headers = strmap_new(pool);
    static_response_headers(pool, headers,
                            istream_file_fd(body), &st,
                            content_type);

    handler->InvokeResponse(handler_ctx, HTTP_STATUS_OK, headers, body);
}
