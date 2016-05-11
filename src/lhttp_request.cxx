/*
 * High level "Local HTTP" client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_request.hxx"
#include "lhttp_stock.hxx"
#include "lhttp_address.hxx"
#include "http_response.hxx"
#include "http_client.hxx"
#include "http_headers.hxx"
#include "stock/Item.hxx"
#include "lease.hxx"
#include "istream/istream.hxx"
#include "header_writer.hxx"
#include "pool.hxx"

struct LhttpRequest final : Lease {
    StockItem &stock_item;

    explicit LhttpRequest(StockItem &_stock_item)
        :stock_item(_stock_item) {}

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        stock_item.Put(!reuse);
    }
};

/*
 * constructor
 *
 */

void
lhttp_request(struct pool &pool, EventLoop &event_loop,
              LhttpStock &lhttp_stock,
              const LhttpAddress &address,
              http_method_t method, HttpHeaders &&headers,
              Istream *body,
              const struct http_response_handler &handler, void *handler_ctx,
              struct async_operation_ref &async_ref)
{
    GError *error = nullptr;
    if (!address.options.Check(&error)) {
        if (body != nullptr)
            body->CloseUnused();

        handler.InvokeAbort(handler_ctx, error);
        return;
    }

    StockItem *stock_item =
        lhttp_stock_get(&lhttp_stock, &pool, &address,
                        &error);
    if (stock_item == nullptr) {
        if (body != nullptr)
            body->CloseUnused();

        handler.InvokeAbort(handler_ctx, error);
        return;
    }

    auto request = NewFromPool<LhttpRequest>(pool, *stock_item);

    if (address.host_and_port != nullptr)
        headers.Write(pool, "host", address.host_and_port);

    http_client_request(pool, event_loop,
                        lhttp_stock_item_get_socket(*stock_item),
                        lhttp_stock_item_get_type(*stock_item),
                        *request,
                        stock_item->GetStockName(),
                        nullptr, nullptr,
                        method, address.uri, std::move(headers), body, true,
                        handler, handler_ctx,
                        async_ref);
}
