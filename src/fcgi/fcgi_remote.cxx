/*
 * High level FastCGI client for remote FastCGI servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi_remote.hxx"
#include "fcgi_client.hxx"
#include "http_response.hxx"
#include "lease.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "stock.hxx"
#include "abort_close.hxx"
#include "address_list.hxx"
#include "pool.hxx"
#include "istream/istream.hxx"
#include "istream/istream_hold.hxx"
#include "net/SocketAddress.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct FcgiRemoteRequest {
    struct pool *pool;

    TcpBalancer *tcp_balancer;

    StockItem *stock_item;

    http_method_t method;
    const char *uri;
    const char *script_filename;
    const char *script_name;
    const char *path_info;
    const char *query_string;
    const char *document_root;
    const char *remote_addr;
    struct strmap *headers;
    struct istream *body;

    ConstBuffer<const char *> params;

    int stderr_fd;

    struct http_response_handler_ref handler;
    struct async_operation_ref *async_ref;
};


/*
 * socket lease
 *
 */

static void
fcgi_socket_release(bool reuse, void *ctx)
{
    FcgiRemoteRequest *request = (FcgiRemoteRequest *)ctx;

    tcp_balancer_put(*request->tcp_balancer, *request->stock_item, !reuse);
}

static const struct lease fcgi_socket_lease = {
    .release = fcgi_socket_release,
};


/*
 * stock callback
 *
 */

static void
fcgi_remote_stock_ready(StockItem &item, void *ctx)
{
    FcgiRemoteRequest *request = (FcgiRemoteRequest *)ctx;

    request->stock_item = &item;

    fcgi_client_request(request->pool, tcp_stock_item_get(item),
                        tcp_stock_item_get_domain(item) == AF_LOCAL
                        ? FdType::FD_SOCKET : FdType::FD_TCP,
                        &fcgi_socket_lease, request,
                        request->method, request->uri,
                        request->script_filename,
                        request->script_name, request->path_info,
                        request->query_string,
                        request->document_root,
                        request->remote_addr,
                        request->headers, request->body,
                        request->params,
                        request->stderr_fd,
                        request->handler.handler, request->handler.ctx,
                        request->async_ref);
}

static void
fcgi_remote_stock_error(GError *error, void *ctx)
{
    FcgiRemoteRequest *request = (FcgiRemoteRequest *)ctx;

    if (request->stderr_fd >= 0)
        close(request->stderr_fd);

    request->handler.InvokeAbort(error);
}

static constexpr StockGetHandler fcgi_remote_stock_handler = {
    .ready = fcgi_remote_stock_ready,
    .error = fcgi_remote_stock_error,
};


/*
 * constructor
 *
 */

void
fcgi_remote_request(struct pool *pool, TcpBalancer *tcp_balancer,
                    const AddressList *address_list,
                    const char *path,
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
    auto request = NewFromPool<FcgiRemoteRequest>(*pool);
    request->pool = pool;
    request->tcp_balancer = tcp_balancer;
    request->method = method;
    request->uri = uri;
    request->script_filename = path;
    request->script_name = script_name;
    request->path_info = path_info;
    request->query_string = query_string;
    request->document_root = document_root;
    request->remote_addr = remote_addr;
    request->headers = headers;
    request->params = params;

    request->stderr_fd = stderr_fd;

    request->handler.Set(*handler, handler_ctx);
    request->async_ref = async_ref;

    if (body != nullptr) {
        request->body = istream_hold_new(pool, body);
        async_ref = &async_close_on_abort(*pool, *request->body, *async_ref);
    } else
        request->body = nullptr;

    tcp_balancer_get(*tcp_balancer, *pool,
                     false, SocketAddress::Null(),
                     0, *address_list, 20,
                     fcgi_remote_stock_handler, request,
                     *async_ref);
}
