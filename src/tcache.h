/*
 * Cache for translation server responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_TCACHE_H
#define __BENG_TCACHE_H

#include "translate.h"

struct tcache;

struct tcache *
translate_cache_new(pool_t pool, struct hstock *tcp_stock,
                    const char *socket_path);

void
translate_cache_close(struct tcache *tcache);

void
translate_cache(pool_t pool, struct tcache *tcache,
                const struct translate_request *request,
                translate_callback_t callback,
                void *ctx,
                struct async_operation_ref *async_ref);

#endif
