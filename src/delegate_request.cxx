/*
 * This helper library glues delegate_glue and http_response_handler
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "delegate_request.hxx"
#include "delegate_client.hxx"
#include "delegate_glue.hxx"
#include "static_headers.hxx"
#include "http_response.hxx"
#include "strmap.hxx"
#include "istream.h"
#include "istream_file.hxx"
#include "gerrno.h"
#include "pool.hxx"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

struct delegate_get {
    struct pool *pool;
    const char *path;
    const char *content_type;
    struct http_response_handler_ref handler;
};

/*
 * delegate_handler
 *
 */

static void
delegate_get_callback(int fd, void *ctx)
{
    struct delegate_get *get = (struct delegate_get *)ctx;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        GError *error = new_error_errno();
        g_prefix_error(&error, "Failed to stat %s: ", get->path);
        get->handler.InvokeAbort(error);
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        close(fd);
        get->handler.InvokeMessage(*get->pool, HTTP_STATUS_NOT_FOUND,
                                   "Not a regular file");
        return;
    }

    /* XXX handle if-modified-since, ... */

    struct strmap *headers = strmap_new(get->pool);
    static_response_headers(get->pool, headers, fd, &st, get->content_type);

    struct istream *body = istream_file_fd_new(get->pool, get->path,
                                               fd, ISTREAM_FILE, st.st_size);
    get->handler.InvokeResponse(HTTP_STATUS_OK, headers, body);
}

static void
delegate_get_error(GError *error, void *ctx)
{
    struct delegate_get *get = (struct delegate_get *)ctx;

    get->handler.InvokeAbort(error);
}

static const struct delegate_handler delegate_get_handler = {
    .success = delegate_get_callback,
    .error = delegate_get_error,
};

/*
 * public
 *
 */

void
delegate_stock_request(struct hstock *stock, struct pool *pool,
                       const char *helper,
                       const struct child_options *options,
                       const char *path, const char *content_type,
                       const struct http_response_handler *handler, void *ctx,
                       struct async_operation_ref &async_ref)
{
    auto get = NewFromPool<struct delegate_get>(*pool);

    get->pool = pool;
    get->path = path;
    get->content_type = content_type;
    get->handler.Set(*handler, ctx);

    delegate_stock_open(stock, pool,
                        helper, options, path,
                        &delegate_get_handler, get,
                        async_ref);
}
