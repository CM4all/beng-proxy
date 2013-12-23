/*
 * High level WAS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was-glue.h"
#include "was-quark.h"
#include "was-stock.h"
#include "was-client.h"
#include "http_response.h"
#include "lease.h"
#include "tcp-stock.h"
#include "stock.h"
#include "abort-close.h"
#include "jail.h"
#include "istream.h"

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

    const char *const* parameters;
    unsigned num_parameters;

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
    struct was_request *request = ctx;

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
    struct was_request *request = ctx;

    request->stock_item = item;

    const struct was_process *process = was_stock_item_get(item);

    was_client_request(request->pool, process->control_fd,
                       process->input_fd, process->output_fd,
                       &was_socket_lease, request,
                       request->method, request->uri,
                       request->script_name, request->path_info,
                       request->query_string,
                       request->headers, request->body,
                       request->parameters, request->num_parameters,
                       request->handler.handler, request->handler.ctx,
                       request->async_ref);
}

static void
was_stock_error(GError *error, void *ctx)
{
    struct was_request *request = ctx;

    http_response_handler_invoke_abort(&request->handler, error);

    if (request->body != NULL)
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
            const struct jail_params *jail,
            bool user_namespace, bool network_namespace,
            const char *action,
            const char *path,
            const char *const*args, unsigned n_args,
            http_method_t method, const char *uri,
            const char *script_name, const char *path_info,
            const char *query_string,
            struct strmap *headers, struct istream *body,
            const char *const parameters[], unsigned num_parameters,
            const struct http_response_handler *handler,
            void *handler_ctx,
            struct async_operation_ref *async_ref)
{
    struct was_request *request;

    GError *error = NULL;
    if (jail != NULL && !jail_params_check(jail, &error)) {
        if (body != NULL)
            istream_close_unused(body);

        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    if (action == NULL)
        action = path;

    request = p_malloc(pool, sizeof(*request));
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
    request->num_parameters = num_parameters;

    http_response_handler_set(&request->handler, handler, handler_ctx);
    request->async_ref = async_ref;

    if (body != NULL) {
        request->body = istream_hold_new(pool, body);
        async_ref = async_close_on_abort(pool, request->body, async_ref);
    } else
        request->body = NULL;

    was_stock_get(was_stock, pool,
                  jail,
                  user_namespace, network_namespace,
                  action, args, n_args,
                  &was_stock_handler, request,
                  async_ref);
}
