/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_unlock.hxx"
#include "istream/ForwardIstream.hxx"
#include "cache.hxx"

#include <assert.h>

class UnlockIstream final : public ForwardIstream {
    struct cache &cache;
    struct cache_item &item;

public:
    UnlockIstream(struct pool &p, Istream &_input,
                  struct cache &_cache, struct cache_item &_item)
        :ForwardIstream(p, _input,
                        MakeIstreamHandler<UnlockIstream>::handler, this),
         cache(_cache), item(_item) {
        cache_item_lock(&item);
    }

    virtual ~UnlockIstream() {
        cache_item_unlock(&cache, &item);
    }
};

Istream *
istream_unlock_new(struct pool &pool, Istream &input,
                   struct cache &cache, struct cache_item &item)
{
    return NewIstream<UnlockIstream>(pool, input, cache, item);
}
