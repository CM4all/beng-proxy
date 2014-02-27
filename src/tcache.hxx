/*
 * Cache for translation server responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TCACHE_HXX
#define BENG_PROXY_TCACHE_HXX

#include <stdint.h>

struct pool;
struct tcache;
struct tstock;
struct TranslateHandler;
struct TranslateRequest;
struct async_operation_ref;
struct cache_stats;
template<typename T> struct ConstBuffer;

struct tcache *
translate_cache_new(struct pool *pool, struct tstock *stock,
                    unsigned max_size);

void
translate_cache_close(struct tcache *tcache);

void
translate_cache_get_stats(const struct tcache *tcache,
                          struct cache_stats *data);

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
translate_cache(struct pool *pool, struct tcache *tcache,
                const TranslateRequest *request,
                const TranslateHandler *handler, void *ctx,
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
                           const TranslateRequest *request,
                           ConstBuffer<uint16_t> vary,
                           const char *site);

#endif
