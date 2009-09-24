/*
 * Connection pooling for the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TSTOCK_H
#define BENG_PROXY_TSTOCK_H

#include "translate.h"

struct tstock;
struct hstock;

struct tstock *
tstock_new(pool_t pool, struct hstock *tcp_stock, const char *socket_path);

void
tstock_translate(struct tstock *stock, pool_t pool,
                 const struct translate_request *request,
                 translate_callback_t callback, void *ctx,
                 struct async_operation_ref *async_ref);

#endif
