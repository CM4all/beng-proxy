/*
 * This helper library glues delegate_glue and http_response_handler
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "delegate-get.h"
#include "delegate-glue.h"
#include "http-response.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

struct delegate_get {
    pool_t pool;
    const char *path;
    struct http_response_handler_ref handler;
};

static void
delegate_get_callback(int fd, void *ctx)
{
    struct delegate_get *get = ctx;
    int ret;
    struct stat st;
    istream_t body;

    if (fd < 0) {
        http_response_handler_invoke_abort(&get->handler);
        return;
    }

    ret = fstat(fd, &st);
    if (ret < 0) {
        close(fd);
        http_response_handler_invoke_abort(&get->handler);
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        close(fd);
        http_response_handler_invoke_abort(&get->handler);
        return;
    }

    /* XXX handle if-modified-since, ... */
    /* XXX headers */

    body = istream_file_fd_new(get->pool, get->path, fd, st.st_size);
    http_response_handler_invoke_response(&get->handler, HTTP_STATUS_OK, NULL,
                                          body);
}

void
delegate_stock_get(struct hstock *stock, pool_t pool,
                   const char *helper, const char *path,
                   const struct http_response_handler *handler, void *ctx,
                   struct async_operation_ref *async_ref)
{
    struct delegate_get *get = p_malloc(pool, sizeof(*get));

    get->pool = pool;
    get->path = path;
    http_response_handler_set(&get->handler, handler, ctx);

    delegate_stock_open(stock, pool,
                        helper, path,
                        delegate_get_callback, get,
                        async_ref);
}
