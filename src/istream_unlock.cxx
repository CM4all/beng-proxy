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
        :ForwardIstream(p, _input),
         cache(_cache), item(_item) {
        cache_item_lock(&item);
    }

    virtual ~UnlockIstream() {
        cache_item_unlock(&cache, &item);
    }

    bool _FillBucketList(IstreamBucketList &list, GError **error_r) override {
        bool success = input.FillBucketList(list, error_r);
        if (!success)
            Destroy();
        return success;
    }

    size_t _ConsumeBucketList(size_t nbytes) override {
        auto consumed = input.ConsumeBucketList(nbytes);
        Consumed(consumed);
        return consumed;
    }
};

Istream *
istream_unlock_new(struct pool &pool, Istream &input,
                   struct cache &cache, struct cache_item &item)
{
    return NewIstream<UnlockIstream>(pool, input, cache, item);
}
