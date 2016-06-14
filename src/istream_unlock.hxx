/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_UNLOCK_HXX
#define BENG_PROXY_ISTREAM_UNLOCK_HXX

struct pool;
class Istream;
struct Cache;
struct CacheItem;

/**
 * An istream facade which unlocks a cache item after it has been
 * closed.
 */
Istream *
istream_unlock_new(struct pool &pool, Istream &input,
                   Cache &cache, CacheItem &item);

#endif
