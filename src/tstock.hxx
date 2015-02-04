/*
 * Connection pooling for the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TSTOCK_HXX
#define BENG_PROXY_TSTOCK_HXX

struct pool;
struct tstock;
struct StockMap;
struct TranslateHandler;
struct TranslateRequest;
struct async_operation_ref;

struct tstock *
tstock_new(struct pool *pool, StockMap *tcp_stock, const char *socket_path);

void
tstock_translate(struct tstock *stock, struct pool *pool,
                 const TranslateRequest *request,
                 const TranslateHandler *handler, void *ctx,
                 struct async_operation_ref *async_ref);

#endif
