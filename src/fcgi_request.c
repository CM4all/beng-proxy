/*
 * High level FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi_request.h"
#include "fcgi_stock.h"
#include "fcgi_client.h"
#include "http_response.h"
#include "lease.h"
#include "tcp-stock.h"
#include "stock.h"
#include "child_options.h"
#include "istream.h"

#include <daemon/log.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct fcgi_request {
    struct pool *pool;

    struct fcgi_stock *fcgi_stock;
    struct stock_item *stock_item;
};

/*
 * socket lease
 *
 */

static void
fcgi_socket_release(bool reuse, void *ctx)
{
    struct fcgi_request *request = ctx;

    fcgi_stock_put(request->fcgi_stock, request->stock_item, !reuse);
}

static const struct lease fcgi_socket_lease = {
    .release = fcgi_socket_release,
};


/*
 * constructor
 *
 */

void
fcgi_request(struct pool *pool, struct fcgi_stock *fcgi_stock,
             const struct child_options *options,
             const char *action,
             const char *path,
             const char *const*args, unsigned n_args,
             http_method_t method, const char *uri,
             const char *script_name, const char *path_info,
             const char *query_string,
             const char *document_root,
             const char *remote_addr,
             struct strmap *headers, struct istream *body,
             const char *const env[], unsigned n_env,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref)
{
    struct fcgi_request *request;

    GError *error = NULL;
    if (!jail_params_check(&options->jail, &error)) {
        if (body != NULL)
            istream_close_unused(body);

        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    if (action == NULL)
        action = path;

    request = p_malloc(pool, sizeof(*request));
    request->pool = pool;
    request->fcgi_stock = fcgi_stock;

    struct stock_item *stock_item =
        fcgi_stock_get(fcgi_stock, pool, options,
                       action,
                       args, n_args,
                       &error);
    if (stock_item == NULL) {
        if (body != NULL)
            istream_close_unused(body);

        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    request->stock_item = stock_item;

    const char *script_filename = fcgi_stock_translate_path(stock_item, path,
                                                            request->pool);
    document_root = fcgi_stock_translate_path(stock_item, document_root,
                                              request->pool);

    fcgi_client_request(request->pool, fcgi_stock_item_get(stock_item),
                        fcgi_stock_item_get_domain(stock_item) == AF_LOCAL
                        ? ISTREAM_SOCKET : ISTREAM_TCP,
                        &fcgi_socket_lease, request,
                        method, uri,
                        script_filename,
                        script_name, path_info,
                        query_string,
                        document_root,
                        remote_addr,
                        headers, body,
                        env, n_env,
                        handler, handler_ctx,
                        async_ref);
}
