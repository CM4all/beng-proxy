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
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "spawn/ChildOptions.hxx"
#include "istream/istream.hxx"
#include "async.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct FcgiRequest final : Lease {
    struct pool &pool;

    StockItem *stock_item;

    struct async_operation operation;
    struct async_operation_ref async_ref;

    FcgiRequest(struct pool &_pool, StockItem &_stock_item)
        :pool(_pool), stock_item(&_stock_item) {
        operation.Init2<FcgiRequest>();
    }

    void Abort() {
        if (stock_item != nullptr)
            fcgi_stock_aborted(*stock_item);

        async_ref.Abort();
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
             struct async_operation_ref &async_ref)
{
    if (action == nullptr)
        action = path;

    GError *error = nullptr;
    StockItem *stock_item =
        fcgi_stock_get(fcgi_stock, pool, options,
                       action,
                       args,
                       &error);
    if (stock_item == nullptr) {
        if (body != nullptr)
            body->CloseUnused();

        if (stderr_fd >= 0)
            close(stderr_fd);

        handler.InvokeError(error);
        return;
    }

    auto request = NewFromPool<FcgiRequest>(*pool, *pool, *stock_item);

    async_ref.Set(request->operation);

    const char *script_filename = fcgi_stock_translate_path(*stock_item, path,
                                                            &request->pool);
    document_root = fcgi_stock_translate_path(*stock_item, document_root,
                                              &request->pool);

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
                        handler, request->async_ref);
}
