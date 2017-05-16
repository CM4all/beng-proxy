/*
 * Connection pooling for the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TSTOCK_HXX
#define BENG_PROXY_TSTOCK_HXX

struct pool;
class EventLoop;
class TranslateStock;
class SocketAddress;
struct TranslateHandler;
struct TranslateRequest;
class CancellablePointer;

TranslateStock *
tstock_new(EventLoop &event_loop, SocketAddress address, unsigned limit);

void
tstock_free(TranslateStock *stock);

void
tstock_translate(TranslateStock &stock, struct pool &pool,
                 const TranslateRequest &request,
                 const TranslateHandler &handler, void *ctx,
                 CancellablePointer &cancel_ptr);

#endif
