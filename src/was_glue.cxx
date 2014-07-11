/*
 * High level WAS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_glue.hxx"
#include "was_quark.h"
#include "was_stock.hxx"
#include "was_client.hxx"
#include "http_response.hxx"
#include "lease.h"
#include "tcp_stock.hxx"
#include "stock.hxx"
#include "abort-close.h"
#include "child_options.hxx"
#include "istream.h"
#include "pool.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct was_request {
    struct pool *pool;

    struct hstock *was_stock;
    const char *action;
    struct stock_item *stock_item;

    http_method_t method;
    const char *uri;
    const char *script_name;
    const char *path_info;
    const char *query_string;
    struct strmap *headers;
    struct istream *body;

    ConstBuffer<const char *> parameters;

    struct http_response_handler_ref handler;
    struct async_operation_ref *async_ref;
};


/*
 * socket lease
 *
 */

static void
was_socket_release(bool reuse, void *ctx)
{
    struct was_request *request = (struct was_request *)ctx;

    was_stock_put(request->was_stock, request->stock_item, !reuse);
}

static const struct lease was_socket_lease = {
    .release = was_socket_release,
};


/*
 * stock callback
 *
 */

static void
was_stock_ready(struct stock_item *item, void *ctx)
{
    struct was_request *request = (struct was_request *)ctx;

    request->stock_item = item;

    const struct was_process *process = was_stock_item_get(item);

    was_client_request(request->pool, process->control_fd,
                       process->input_fd, process->output_fd,
                       &was_socket_lease, request,
                       request->method, request->uri,
                       request->script_name, request->path_info,
                       request->query_string,
                       request->headers, request->body,
                       request->parameters,
                       request->handler.handler, request->handler.ctx,
                       request->async_ref);
}

static void
was_stock_error(GError *error, void *ctx)
{
    struct was_request *request = (struct was_request *)ctx;

    http_response_handler_invoke_abort(&request->handler, error);

    if (request->body != nullptr)
        istream_close_unused(request->body);
}

static const struct stock_get_handler was_stock_handler = {
    .ready = was_stock_ready,
    .error = was_stock_error,
};


/*
 * constructor
 *
 */

void
was_request(struct pool *pool, struct hstock *was_stock,
            const struct child_options *options,
            const char *action,
            const char *path,
            ConstBuffer<const char *> args,
            ConstBuffer<const char *> env,
            http_method_t method, const char *uri,
            const char *script_name, const char *path_info,
            const char *query_string,
            struct strmap *headers, struct istream *body,
            ConstBuffer<const char *> parameters,
            const struct http_response_handler *handler,
            void *handler_ctx,
            struct async_operation_ref *async_ref)
{
    GError *error = nullptr;
    if (!jail_params_check(&options->jail, &error)) {
        if (body != nullptr)
            istream_close_unused(body);

        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    if (action == nullptr)
        action = path;

    auto request = NewFromPool<struct was_request>(*pool);
    request->pool = pool;
    request->was_stock = was_stock;
    request->action = action;
    request->method = method;
    request->uri = uri;
    request->script_name = script_name;
    request->path_info = path_info;
    request->query_string = query_string;
    request->headers = headers;
    request->parameters = parameters;

    http_response_handler_set(&request->handler, handler, handler_ctx);
    request->async_ref = async_ref;

    if (body != nullptr) {
        request->body = istream_hold_new(pool, body);
        async_ref = async_close_on_abort(pool, request->body, async_ref);
    } else
        request->body = nullptr;

    was_stock_get(was_stock, pool,
                  options,
                  action, args, env,
                  &was_stock_handler, request,
                  async_ref);
}
