/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_unlock.hxx"
#include "istream/ForwardIstream.hxx"
#include "cache.hxx"

#include <assert.h>

class UnlockIstream final : public ForwardIstream {
    CacheItem &item;

public:
    UnlockIstream(struct pool &p, Istream &_input,
                  CacheItem &_item)
        :ForwardIstream(p, _input),
         item(_item) {
        item.Lock();
    }

    virtual ~UnlockIstream() {
        item.Unlock();
    }

    void _FillBucketList(IstreamBucketList &list) override {
        try {
            input.FillBucketList(list);
        } catch (...) {
            Destroy();
            throw;
        }
    }

    size_t _ConsumeBucketList(size_t nbytes) override {
        auto consumed = input.ConsumeBucketList(nbytes);
        Consumed(consumed);
        return consumed;
    }
};

Istream *
istream_unlock_new(struct pool &pool, Istream &input, CacheItem &item)
{
    return NewIstream<UnlockIstream>(pool, input, item);
}
