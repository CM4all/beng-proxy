/*
 * Connection pooling for the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TSTOCK_HXX
#define BENG_PROXY_TSTOCK_HXX

struct pool;
class TranslateStock;
struct StockMap;
struct TranslateHandler;
struct TranslateRequest;
struct async_operation_ref;

TranslateStock *
tstock_new(struct pool &pool, StockMap &tcp_stock, const char *socket_path);

void
tstock_free(struct pool &pool, TranslateStock *stock);

void
tstock_translate(TranslateStock &stock, struct pool &pool,
                 const TranslateRequest &request,
                 const TranslateHandler &handler, void *ctx,
                 struct async_operation_ref &async_ref);

#endif
