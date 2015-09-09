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
#include "lease.hxx"
#include "istream/istream.hxx"
#include "header_writer.hxx"
#include "pool.hxx"

struct LhttpRequest {
    LhttpStock *lhttp_stock;
    StockItem *stock_item;
};

/*
 * socket lease
 *
 */

static void
lhttp_socket_release(bool reuse, void *ctx)
{
    auto *request = (LhttpRequest *)ctx;

    lhttp_stock_put(request->lhttp_stock, *request->stock_item, !reuse);
}

static const struct lease lhttp_socket_lease = {
    .release = lhttp_socket_release,
};

/*
 * constructor
 *
 */

void
lhttp_request(struct pool &pool, LhttpStock &lhttp_stock,
              const LhttpAddress &address,
              http_method_t method, HttpHeaders &&headers,
              struct istream *body,
              const struct http_response_handler &handler, void *handler_ctx,
              struct async_operation_ref &async_ref)
{
    GError *error = nullptr;
    if (!address.options.Check(&error)) {
        if (body != nullptr)
            istream_close(body);

        handler.InvokeAbort(handler_ctx, error);
        return;
    }

    StockItem *stock_item =
        lhttp_stock_get(&lhttp_stock, &pool, &address,
                        &error);
    if (stock_item == nullptr) {
        if (body != nullptr)
            istream_close(body);

        handler.InvokeAbort(handler_ctx, error);
        return;
    }

    auto request = NewFromPool<LhttpRequest>(pool);
    request->lhttp_stock = &lhttp_stock;
    request->stock_item = stock_item;

    if (address.host_and_port != nullptr)
        headers.Write(pool, "host", address.host_and_port);

    http_client_request(pool,
                        lhttp_stock_item_get_socket(*stock_item),
                        lhttp_stock_item_get_type(*stock_item),
                        lhttp_socket_lease, request,
                        lhttp_stock_item_get_name(*stock_item),
                        nullptr, nullptr,
                        method, address.uri, std::move(headers), body, true,
                        handler, handler_ctx,
                        async_ref);
}
