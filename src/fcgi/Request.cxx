/*
 * High level FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Request.hxx"
#include "Stock.hxx"
#include "Client.hxx"
#include "http_response.hxx"
#include "lease.hxx"
#include "tcp_stock.hxx"
#include "stock/Item.hxx"
#include "istream/istream.hxx"
#include "pool.hxx"
#include "AllocatorPtr.hxx"
#include "net/SocketDescriptor.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cancellable.hxx"

#include <sys/socket.h>
#include <unistd.h>

struct FcgiRequest final : Lease, Cancellable {
    struct pool &pool;

    StockItem *stock_item;

    CancellablePointer cancel_ptr;

    FcgiRequest(struct pool &_pool, StockItem &_stock_item)
        :pool(_pool), stock_item(&_stock_item) {
    }

    /* virtual methods from class Cancellable */
    void Cancel() override {
        if (stock_item != nullptr)
            fcgi_stock_aborted(*stock_item);

        cancel_ptr.Cancel();
    }

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        stock_item->Put(!reuse);
        stock_item = nullptr;
    }
};

void
fcgi_request(struct pool *pool, EventLoop &event_loop,
             FcgiStock *fcgi_stock,
             const ChildOptions &options,
             const char *action,
             const char *path,
             ConstBuffer<const char *> args,
             http_method_t method, const char *uri,
             const char *script_name, const char *path_info,
             const char *query_string,
             const char *document_root,
             const char *remote_addr,
             const StringMap &headers, Istream *body,
             ConstBuffer<const char *> params,
             int stderr_fd,
             HttpResponseHandler &handler,
             CancellablePointer &cancel_ptr)
{
    if (action == nullptr)
        action = path;

    StockItem *stock_item;
    try {
        stock_item = fcgi_stock_get(fcgi_stock, pool, options,
                                    action,
                                    args);
    } catch (...) {
        if (body != nullptr)
            body->CloseUnused();

        if (stderr_fd >= 0)
            close(stderr_fd);

        handler.InvokeError(std::current_exception());
        return;
    }

    auto request = NewFromPool<FcgiRequest>(*pool, *pool, *stock_item);

    cancel_ptr = *request;

    const char *script_filename = fcgi_stock_translate_path(*stock_item, path,
                                                            request->pool);
    document_root = fcgi_stock_translate_path(*stock_item, document_root,
                                              request->pool);

    fcgi_client_request(&request->pool, event_loop,
                        fcgi_stock_item_get(*stock_item),
                        fcgi_stock_item_get_domain(*stock_item) == AF_LOCAL
                        ? FdType::FD_SOCKET : FdType::FD_TCP,
                        *request,
                        method, uri,
                        script_filename,
                        script_name, path_info,
                        query_string,
                        document_root,
                        remote_addr,
                        headers, body,
                        params,
                        stderr_fd,
                        handler, request->cancel_ptr);
}
