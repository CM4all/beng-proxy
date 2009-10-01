/*
 * This helper library glues delegate_glue and http_response_handler
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "delegate-request.h"
#include "delegate-glue.h"
#include "static-headers.h"
#include "http-response.h"
#include "http-error.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

struct delegate_get {
    pool_t pool;
    const char *path;
    const char *content_type;
    struct http_response_handler_ref handler;
};

static void
delegate_get_callback(int fd, void *ctx)
{
    struct delegate_get *get = ctx;
    int ret;
    struct stat st;
    struct strmap *headers;
    istream_t body;

    if (fd < 0) {
        http_response_handler_invoke_errno(&get->handler, get->pool, -fd);
        return;
    }

    ret = fstat(fd, &st);
    if (ret < 0) {
        int error = errno;
        close(fd);
        http_response_handler_invoke_errno(&get->handler, get->pool, error);
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        close(fd);
        http_response_handler_invoke_message(&get->handler, get->pool,
                                             HTTP_STATUS_NOT_FOUND,
                                             "Not a regular file");
        return;
    }

    /* XXX handle if-modified-since, ... */

    headers = strmap_new(get->pool, 13);
    static_response_headers(get->pool, headers, fd, &st, get->content_type);

    body = istream_file_fd_new(get->pool, get->path, fd, st.st_size);
    http_response_handler_invoke_response(&get->handler, HTTP_STATUS_OK,
                                          headers, body);
}

void
delegate_stock_request(struct hstock *stock, pool_t pool,
                       const char *helper, const char *document_root,
                       const char *path, const char *content_type,
                       const struct http_response_handler *handler, void *ctx,
                       struct async_operation_ref *async_ref)
{
    struct delegate_get *get = p_malloc(pool, sizeof(*get));

    get->pool = pool;
    get->path = path;
    get->content_type = content_type;
    http_response_handler_set(&get->handler, handler, ctx);

    delegate_stock_open(stock, pool,
                        helper, document_root, path,
                        delegate_get_callback, get,
                        async_ref);
}
