/*
 * This helper library glues delegate_glue and http_response_handler
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DELEGATE_REQUEST_HXX
#define BENG_PROXY_DELEGATE_REQUEST_HXX

struct pool;
struct StockMap;
struct http_response_handler;
struct async_operation_ref;
struct ChildOptions;

void
delegate_stock_request(StockMap *stock, struct pool *pool,
                       const char *helper,
                       const ChildOptions &options,
                       const char *path, const char *content_type,
                       const struct http_response_handler *handler, void *ctx,
                       struct async_operation_ref &async_ref);

#endif
