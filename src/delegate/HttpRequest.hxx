/*
 * This helper library glues delegate_glue and http_response_handler
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DELEGATE_HTTP_REQUEST_HXX
#define BENG_PROXY_DELEGATE_HTTP_REQUEST_HXX

struct pool;
class StockMap;
class HttpResponseHandler;
struct ChildOptions;
class EventLoop;
class CancellablePointer;

void
delegate_stock_request(EventLoop &event_loop, StockMap &stock,
                       struct pool &pool,
                       const char *helper,
                       const ChildOptions &options,
                       const char *path, const char *content_type,
                       HttpResponseHandler &handler,
                       CancellablePointer &cancel_ptr);

#endif
