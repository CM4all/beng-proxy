/*
 * High level WAS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_glue.hxx"
#include "was_quark.h"
#include "was_stock.hxx"
#include "was_launch.hxx"
#include "was_client.hxx"
#include "http_response.hxx"
#include "Lease.hxx"
#include "tcp_stock.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "abort_close.hxx"
#include "spawn/ChildOptions.hxx"
#include "istream/istream.hxx"
#include "istream/istream_hold.hxx"
#include "pool.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

class WasRequest final : public StockGetHandler, WasLease {
    struct pool &pool;

    StockItem *stock_item;

    http_method_t method;
    const char *uri;
    const char *script_name;
    const char *path_info;
    const char *query_string;
    StringMap &headers;
    Istream *body = nullptr;

    ConstBuffer<const char *> parameters;

    struct http_response_handler_ref handler;
    struct async_operation_ref &async_ref;

public:
    WasRequest(struct pool &_pool,
               http_method_t _method, const char *_uri,
               const char *_script_name, const char *_path_info,
               const char *_query_string,
               StringMap &_headers,
               ConstBuffer<const char *> _parameters,
               const struct http_response_handler &_handler,
               void *_handler_ctx,
               struct async_operation_ref &_async_ref)
        :pool(_pool),
         method(_method),
         uri(_uri), script_name(_script_name),
         path_info(_path_info), query_string(_query_string),
         headers(_headers), parameters(_parameters),
         async_ref(_async_ref) {
        handler.Set(_handler, _handler_ctx);
    }

    struct async_operation_ref *SetBody(Istream *_body,
                                        struct async_operation_ref *_async_ref) {
        assert(body == nullptr);

        if (_body != nullptr) {
            body = istream_hold_new(pool, *_body);
            _async_ref = &async_close_on_abort(pool, *body, *_async_ref);
        }

        return _async_ref;
    }

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;

private:
    /* virtual methods from class WasLease */
    void ReleaseWas(bool reuse) override {
        stock_item->Put(!reuse);
    }

    void ReleaseWasStop(uint64_t input_received) override {
        was_stock_item_stop(*stock_item, input_received);
        stock_item->Put(false);
    }
};

/*
 * stock callback
 *
 */

void
WasRequest::OnStockItemReady(StockItem &item)
{
    stock_item = &item;

    const auto &process = was_stock_item_get(item);

    was_client_request(pool, item.stock.GetEventLoop(), process.control_fd,
                       process.input_fd, process.output_fd,
                       *this,
                       method, uri,
                       script_name, path_info,
                       query_string,
                       headers, body,
                       parameters,
                       *handler.handler, handler.ctx,
                       async_ref);
}

void
WasRequest::OnStockItemError(GError *error)
{
    handler.InvokeAbort(error);

    if (body != nullptr)
        body->CloseUnused();
}

/*
 * constructor
 *
 */

void
was_request(struct pool &pool, StockMap &was_stock,
            const ChildOptions &options,
            const char *action,
            const char *path,
            ConstBuffer<const char *> args,
            http_method_t method, const char *uri,
            const char *script_name, const char *path_info,
            const char *query_string,
            StringMap &headers, Istream *body,
            ConstBuffer<const char *> parameters,
            const struct http_response_handler &handler,
            void *handler_ctx,
            struct async_operation_ref &async_ref)
{
    if (action == nullptr)
        action = path;

    auto request = NewFromPool<WasRequest>(pool, pool,
                                           method, uri, script_name,
                                           path_info, query_string,
                                           headers, parameters,
                                           handler, handler_ctx,
                                           async_ref);

    was_stock_get(&was_stock, &pool,
                  options,
                  action, args,
                  *request, *request->SetBody(body, &async_ref));
}
