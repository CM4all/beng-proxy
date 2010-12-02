/*
 * Cache for translation server responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_TCACHE_H
#define __BENG_TCACHE_H

#include "translate.h"

struct tcache;
struct tstock;

struct tcache *
translate_cache_new(pool_t pool, struct tstock *stock,
                    unsigned max_size);

void
translate_cache_close(struct tcache *tcache);

/**
 * Flush all items from the cache.
 */
void
translate_cache_flush(struct tcache *tcache);

/**
 * Query an item from the cache.  If not present, the request is
 * forwarded to the "real" translation server, and its response is
 * added to the cache.
 */
void
translate_cache(pool_t pool, struct tcache *tcache,
                const struct translate_request *request,
                translate_callback_t callback,
                void *ctx,
                struct async_operation_ref *async_ref);

/**
 * Flush selected items from the cache.
 *
 * @param request a request with parameters to compare with
 * @param vary a list of #beng_translation_command codes which define
 * the cache item filter
 */
void
translate_cache_invalidate(struct tcache *tcache,
                           const struct translate_request *request,
                           const uint16_t *vary, unsigned num_vary,
                           const char *site);

#endif
