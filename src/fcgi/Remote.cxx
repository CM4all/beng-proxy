/*
 * High level FastCGI client for remote FastCGI servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Remote.hxx"
#include "Client.hxx"
#include "http_response.hxx"
#include "lease.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "abort_close.hxx"
#include "address_list.hxx"
#include "pool.hxx"
#include "strmap.hxx"
#include "async.hxx"
#include "istream/istream.hxx"
#include "istream/istream_hold.hxx"
#include "net/SocketAddress.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct FcgiRemoteRequest final : StockGetHandler, Lease {
    struct pool &pool;
    EventLoop &event_loop;

    StockItem *stock_item;

    const http_method_t method;
    const char *const uri;
    const char *const script_filename;
    const char *const script_name;
    const char *const path_info;
    const char *const query_string;
    const char *const document_root;
    const char *const remote_addr;
    const StringMap headers;
    Istream *body;

    const ConstBuffer<const char *> params;

    const int stderr_fd;

    HttpResponseHandler &handler;
    CancellablePointer &cancel_ptr;

    FcgiRemoteRequest(struct pool &_pool, EventLoop &_event_loop,
                      http_method_t _method, const char *_uri,
                      const char *_script_filename,
                      const char *_script_name, const char *_path_info,
                      const char *_query_string,
                      const char *_document_root,
                      const char *_remote_addr,
                      StringMap &&_headers,
                      ConstBuffer<const char *> _params,
                      int _stderr_fd,
                      HttpResponseHandler &_handler,
                      CancellablePointer &_cancel_ptr)
        :pool(_pool), event_loop(_event_loop),
         method(_method), uri(_uri),
         script_filename(_script_filename), script_name(_script_name),
         path_info(_path_info), query_string(_query_string),
         document_root(_document_root),
         remote_addr(_remote_addr),
         headers(std::move(_headers)),
         params(_params),
         stderr_fd(_stderr_fd),
         handler(_handler), cancel_ptr(_cancel_ptr) {
    }

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        stock_item->Put(!reuse);
    }
};

/*
 * stock callback
 *
 */

void
FcgiRemoteRequest::OnStockItemReady(StockItem &item)
{
    stock_item = &item;

    fcgi_client_request(&pool, event_loop, tcp_stock_item_get(item),
                        tcp_stock_item_get_domain(item) == AF_LOCAL
                        ? FdType::FD_SOCKET : FdType::FD_TCP,
                        *this,
                        method, uri,
                        script_filename,
                        script_name, path_info,
                        query_string,
                        document_root,
                        remote_addr,
                        headers, body,
                        params,
                        stderr_fd,
                        handler,
                        cancel_ptr);
}

void
FcgiRemoteRequest::OnStockItemError(GError *error)
{
    if (stderr_fd >= 0)
        close(stderr_fd);

    handler.InvokeError(error);
}

/*
 * constructor
 *
 */

void
fcgi_remote_request(struct pool *pool, EventLoop &event_loop,
                    TcpBalancer *tcp_balancer,
                    const AddressList *address_list,
                    const char *path,
                    http_method_t method, const char *uri,
                    const char *script_name, const char *path_info,
                    const char *query_string,
                    const char *document_root,
                    const char *remote_addr,
                    StringMap &&headers, Istream *body,
                    ConstBuffer<const char *> params,
                    int stderr_fd,
                    HttpResponseHandler &handler,
                    struct async_operation_ref &_async_ref)
{
    CancellablePointer *cancel_ptr = &_async_ref;
    auto request = NewFromPool<FcgiRemoteRequest>(*pool, *pool, event_loop,
                                                  method, uri, path,
                                                  script_name, path_info,
                                                  query_string, document_root,
                                                  remote_addr,
                                                  std::move(headers), params,
                                                  stderr_fd,
                                                  handler, *cancel_ptr);

    if (body != nullptr) {
        request->body = istream_hold_new(*pool, *body);
        cancel_ptr = &async_close_on_abort(*pool, *request->body, *cancel_ptr);
    } else
        request->body = nullptr;

    tcp_balancer_get(*tcp_balancer, *pool,
                     false, SocketAddress::Null(),
                     0, *address_list, 20,
                     *request, *cancel_ptr);
}
