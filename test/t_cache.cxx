#include "cache.hxx"
#include "pool.hxx"
#include "RootPool.hxx"
#include "event/Event.hxx"

#include <assert.h>
#include <time.h>

static void *
match_to_ptr(int match)
{
    return (void*)(long)match;
}

static int
ptr_to_match(void *p)
{
    return (int)(long)p;
}

struct MyCacheItem final : CacheItem {
    struct pool *const pool;
    const int match;
    const int value;

    MyCacheItem(struct pool &_pool, int _match, int _value)
        :CacheItem(std::chrono::hours(1), 1),
         pool(&_pool), match(_match), value(_value) {
    }

    /* virtual methods from class CacheItem */
    void Destroy() override {
        struct pool *_pool = pool;

        p_free(_pool, this);
        pool_unref(_pool);
    }
};

static MyCacheItem *
my_cache_item_new(struct pool *pool, int match, int value)
{
    pool = pool_new_linear(pool, "my_cache_item", 1024);
    auto i = NewFromPool<MyCacheItem>(*pool, *pool, match, value);
    return i;
}

static bool
my_match(const CacheItem *item, void *ctx)
{
    const MyCacheItem *i = (const MyCacheItem *)item;
    int match = ptr_to_match(ctx);

    return i->match == match;
}

int main(int argc gcc_unused, char **argv gcc_unused) {
    MyCacheItem *i;

    EventLoop event_loop;

    RootPool pool;

    Cache *cache = cache_new(*pool, event_loop, 1024, 4);

    /* add first item */

    i = my_cache_item_new(pool, 1, 0);
    cache_put(cache, "foo", i);

    /* overwrite first item */

    i = my_cache_item_new(pool, 2, 0);
    cache_put(cache, "foo", i);

    /* check overwrite result */

    i = (MyCacheItem *)cache_get(cache, "foo");
    assert(i != nullptr);
    assert(i->match == 2);
    assert(i->value == 0);

    i = (MyCacheItem *)cache_get_match(cache, "foo",
                                                my_match, match_to_ptr(1));
    assert(i == nullptr);

    i = (MyCacheItem *)cache_get_match(cache, "foo",
                                                my_match, match_to_ptr(2));
    assert(i != nullptr);
    assert(i->match == 2);
    assert(i->value == 0);

    /* add new item */

    i = my_cache_item_new(pool, 1, 1);
    cache_put_match(cache, "foo", i, my_match, match_to_ptr(1));

    /* check second item */

    i = (MyCacheItem *)cache_get_match(cache, "foo",
                                                my_match, match_to_ptr(1));
    assert(i != nullptr);
    assert(i->match == 1);
    assert(i->value == 1);

    /* check first item */

    i = (MyCacheItem *)cache_get_match(cache, "foo",
                                                my_match, match_to_ptr(2));
    assert(i != nullptr);
    assert(i->match == 2);
    assert(i->value == 0);

    /* overwrite first item */

    i = my_cache_item_new(pool, 1, 3);
    cache_put_match(cache, "foo", i, my_match, match_to_ptr(1));

    i = (MyCacheItem *)cache_get_match(cache, "foo",
                                                my_match, match_to_ptr(1));
    assert(i != nullptr);
    assert(i->match == 1);
    assert(i->value == 3);

    i = (MyCacheItem *)cache_get_match(cache, "foo",
                                                my_match, match_to_ptr(2));
    assert(i != nullptr);
    assert(i->match == 2);
    assert(i->value == 0);

    /* overwrite second item */

    i = my_cache_item_new(pool, 2, 4);
    cache_put_match(cache, "foo", i, my_match, match_to_ptr(2));

    i = (MyCacheItem *)cache_get_match(cache, "foo",
                                                my_match, match_to_ptr(1));
    assert(i != nullptr);
    assert(i->match == 1);
    assert(i->value == 3);

    i = (MyCacheItem *)cache_get_match(cache, "foo",
                                                my_match, match_to_ptr(2));
    assert(i != nullptr);
    assert(i->match == 2);
    assert(i->value == 4);

    /* cleanup */

    cache_close(cache);
}
