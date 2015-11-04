/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_UNLOCK_HXX
#define BENG_PROXY_ISTREAM_UNLOCK_HXX

struct pool;
class Istream;
struct cache;
struct cache_item;

/**
 * An istream facade which unlocks a cache item after it has been
 * closed.
 */
Istream *
istream_unlock_new(struct pool &pool, Istream &input,
                   struct cache &cache, struct cache_item &item);

#endif
