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
    UnlockIstream(struct pool &p, struct istream &_input,
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

struct istream *
istream_unlock_new(struct pool *pool, struct istream *input,
                   struct cache *cache, struct cache_item *item)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));
    assert(cache != nullptr);
    assert(item != nullptr);

    return NewIstream<UnlockIstream>(*pool, *input, *cache, *item);
}
