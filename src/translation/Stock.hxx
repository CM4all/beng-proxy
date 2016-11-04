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
struct TranslateHandler;
struct TranslateRequest;
class CancellablePointer;

TranslateStock *
tstock_new(EventLoop &event_loop, const char *socket_path, unsigned limit);

void
tstock_free(TranslateStock *stock);

void
tstock_translate(TranslateStock &stock, struct pool &pool,
                 const TranslateRequest &request,
                 const TranslateHandler &handler, void *ctx,
                 CancellablePointer &cancel_ptr);

#endif
