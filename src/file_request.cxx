/*
 * Static file support for DirectResourceLoader.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_request.hxx"
#include "static_headers.hxx"
#include "http_response.hxx"
#include "gerrno.h"
#include "strmap.hxx"
#include "istream/istream.hxx"
#include "istream/istream_file.hxx"
#include "pool.hxx"

#include <http/status.h>

#include <assert.h>
#include <sys/stat.h>

void
static_file_get(EventLoop &event_loop, struct pool &pool,
                const char *path, const char *content_type,
                HttpResponseHandler &handler)
{
    assert(path != nullptr);

    struct stat st;
    if (lstat(path, &st) != 0) {
        GError *error = new_error_errno();
        g_prefix_error(&error, "Failed to open %s: ", path);
        handler.InvokeError(error);
        return;
    }

    if (!S_ISREG(st.st_mode) && !S_ISCHR(st.st_mode)) {
        handler.InvokeResponse(pool, HTTP_STATUS_NOT_FOUND,
                               "Not a regular file");
        return;
    }

    const off_t size = S_ISCHR(st.st_mode)
        ? -1 : st.st_size;

    GError *error = nullptr;
    Istream *body = istream_file_new(event_loop, pool, path, size, &error);
    if (body == nullptr) {
        handler.InvokeError(error);
        return;
    }

    handler.InvokeResponse(HTTP_STATUS_OK,
                           static_response_headers(pool,
                                                   istream_file_fd(*body), st,
                                                   content_type),
                           body);
}
