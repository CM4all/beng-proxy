/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_UNLOCK_HXX
#define BENG_PROXY_ISTREAM_UNLOCK_HXX

struct pool;
struct istream;
struct cache;
struct cache_item;

/**
 * An istream facade which unlocks a cache item after it has been
 * closed.
 */
struct istream *
istream_unlock_new(struct pool *pool, struct istream *input,
                   struct cache *cache, struct cache_item *item);

#endif
