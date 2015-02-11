/*
 * High level FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi_request.hxx"
#include "fcgi_stock.hxx"
#include "fcgi_client.hxx"
#include "http_response.hxx"
#include "lease.hxx"
#include "tcp_stock.hxx"
#include "stock.hxx"
#include "child_options.hxx"
#include "istream.h"
#include "async.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct fcgi_request {
    struct pool *pool;

    struct fcgi_stock *fcgi_stock;
    StockItem *stock_item;

    struct async_operation async;
    struct async_operation_ref async_ref;
};

/*
 * socket lease
 *
 */

static void
fcgi_socket_release(bool reuse, void *ctx)
{
    struct fcgi_request *request = (struct fcgi_request *)ctx;

    fcgi_stock_put(request->fcgi_stock, *request->stock_item, !reuse);
    request->stock_item = nullptr;
}

static const struct lease fcgi_socket_lease = {
    .release = fcgi_socket_release,
};


/*
 * async operation
 *
 */

static void
fcgi_request_abort(struct async_operation *ao)
{
    struct fcgi_request &request = ContainerCast2(*ao, &fcgi_request::async);

    if (request.stock_item != nullptr)
        fcgi_stock_aborted(*request.stock_item);

    request.async_ref.Abort();
}

static const struct async_operation_class fcgi_request_async_operation = {
    .abort = fcgi_request_abort,
};


/*
 * constructor
 *
 */

void
fcgi_request(struct pool *pool, struct fcgi_stock *fcgi_stock,
             const ChildOptions &options,
             const char *action,
             const char *path,
             ConstBuffer<const char *> args,
             ConstBuffer<const char *> env,
             http_method_t method, const char *uri,
             const char *script_name, const char *path_info,
             const char *query_string,
             const char *document_root,
             const char *remote_addr,
             struct strmap *headers, struct istream *body,
             ConstBuffer<const char *> params,
             int stderr_fd,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref)
{
    GError *error = nullptr;
    if (!jail_params_check(&options.jail, &error)) {
        if (body != nullptr)
            istream_close_unused(body);

        if (stderr_fd >= 0)
            close(stderr_fd);

        handler->InvokeAbort(handler_ctx, error);
        return;
    }

    if (action == nullptr)
        action = path;

    auto request = NewFromPool<struct fcgi_request>(*pool);
    request->pool = pool;
    request->fcgi_stock = fcgi_stock;

    StockItem *stock_item =
        fcgi_stock_get(fcgi_stock, pool, options,
                       action,
                       args, env,
                       &error);
    if (stock_item == nullptr) {
        if (body != nullptr)
            istream_close_unused(body);

        if (stderr_fd >= 0)
            close(stderr_fd);

        handler->InvokeAbort(handler_ctx, error);
        return;
    }

    request->stock_item = stock_item;

    request->async.Init(fcgi_request_async_operation);
    async_ref->Set(request->async);
    async_ref = &request->async_ref;

    const char *script_filename = fcgi_stock_translate_path(*stock_item, path,
                                                            request->pool);
    document_root = fcgi_stock_translate_path(*stock_item, document_root,
                                              request->pool);

    fcgi_client_request(request->pool, fcgi_stock_item_get(*stock_item),
                        fcgi_stock_item_get_domain(*stock_item) == AF_LOCAL
                        ? ISTREAM_SOCKET : ISTREAM_TCP,
                        &fcgi_socket_lease, request,
                        method, uri,
                        script_filename,
                        script_name, path_info,
                        query_string,
                        document_root,
                        remote_addr,
                        headers, body,
                        params,
                        stderr_fd,
                        handler, handler_ctx,
                        async_ref);
}
