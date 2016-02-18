/*
 * Connection pooling for the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TSTOCK_HXX
#define BENG_PROXY_TSTOCK_HXX

struct pool;
class TranslateStock;
struct TranslateHandler;
struct TranslateRequest;
struct async_operation_ref;

TranslateStock *
tstock_new(const char *socket_path, unsigned limit);

void
tstock_free(TranslateStock *stock);

void
tstock_translate(TranslateStock &stock, struct pool &pool,
                 const TranslateRequest &request,
                 const TranslateHandler &handler, void *ctx,
                 struct async_operation_ref &async_ref);

#endif
