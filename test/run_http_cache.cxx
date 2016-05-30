#include "pool.hxx"
#include "tpool.hxx"
#include "RootPool.hxx"
#include "cache.hxx"
#include "http_cache_heap.hxx"
#include "http_cache_info.hxx"
#include "strmap.hxx"
#include "rubber.hxx"
#include "AllocatorStats.hxx"
#include "event/Loop.hxx"

#include <inline/compiler.h>

#include <stdlib.h>

static void
put_random(HttpCacheHeap *cache, Rubber *rubber)
{
    const AutoRewindPool auto_rewind(*tpool);

    char uri[8];
    uri[0] = '0' + random() % 10;
    uri[1] = '0' + random() % 10;
    uri[2] = '0' + random() % 10;
    uri[3] = '0' + random() % 10;
    uri[4] = '0' + random() % 10;
    uri[5] = 0;

    HttpCacheResponseInfo info;
    info.expires = 1350000000;
    info.vary = "x-foo";

    size_t length = random() % (random() % (random() % (64 * 1024) + 1) + 1);
    unsigned rubber_id = 0;
    if (length > 0) {
        rubber_id = rubber_add(rubber, length);
        if (rubber_id == 0) {
            fprintf(stderr, "rubber_add(%zu) failed\n", length);
            return;
        }
    }

    struct strmap *request_headers = strmap_new(tpool);

    if (random() % 3 == 0) {
        static const char *const values[] = {
            "a", "b", "c", "d", "e", "f", "g", "h",
        };
        request_headers->Add("x-foo", values[random() % 8]);
    }

    struct strmap *response_headers = strmap_new(tpool);
    response_headers->Add("content-type", "text/plain");
    response_headers->Add("x-foo", "bar");
    response_headers->Add("x-bar", "foo");

    cache->Put(uri, info, request_headers,
               HTTP_STATUS_OK, response_headers,
               *rubber, rubber_id, length);
}

/*
 * main
 *
 */

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    static const size_t max_size = 256 * 1024 * 1024;

    Rubber *rubber = rubber_new(max_size);
    if (rubber == NULL)
        return EXIT_FAILURE;

    EventLoop event_loop;

    RootPool pool;

    struct pool *pool2 = pool_new_libc(pool, "cache");

    HttpCacheHeap cache;
    cache.Init(*pool2, event_loop, max_size);

    for (unsigned i = 0; i < 32 * 1024; ++i)
        put_random(&cache, rubber);

    const auto stats = cache.GetStats(*rubber);
    printf("netto=%zu brutto=%zu ratio=%f\n",
           stats.netto_size, stats.brutto_size,
           (double)stats.netto_size / stats.brutto_size);

    cache.Deinit();

    pool_unref(pool2);

    rubber_free(rubber);

    return EXIT_SUCCESS;
}
