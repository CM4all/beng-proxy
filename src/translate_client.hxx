/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_CLIENT_HXX
#define BENG_PROXY_TRANSLATE_CLIENT_HXX

struct pool;
struct lease;
struct async_operation_ref;
struct TranslateRequest;
struct TranslateHandler;

void
translate(struct pool &pool, int fd,
          const struct lease &lease, void *lease_ctx,
          const TranslateRequest &request,
          const TranslateHandler &handler, void *ctx,
          struct async_operation_ref &async_ref);

#endif
