#include "pool.h"
#include "tpool.h"
#include "cache.h"
#include "http-cache-heap.h"
#include "http-cache-internal.h"
#include "strmap.h"
#include "rubber.h"

#include <inline/compiler.h>

#include <event.h>

#include <stdlib.h>

static void
put_random(struct http_cache_heap *cache, struct rubber *rubber)
{
    struct pool_mark_state mark;
    pool_mark(tpool, &mark);

    char uri[8];
    uri[0] = '0' + random() % 10;
    uri[1] = '0' + random() % 10;
    uri[2] = '0' + random() % 10;
    uri[3] = '0' + random() % 10;
    uri[4] = '0' + random() % 10;
    uri[5] = 0;

    static const struct http_cache_info info = {
        .expires = 1350000000,
        .vary = "x-foo",
    };

    size_t length = random() % (random() % (random() % (64 * 1024) + 1) + 1);
    unsigned rubber_id = 0;
    if (length > 0) {
        rubber_id = rubber_add(rubber, length);
        if (rubber_id == 0) {
            fprintf(stderr, "rubber_add(%zu) failed\n", length);
            return;
        }
    }

    struct strmap *request_headers = strmap_new(tpool, 7);

    if (random() % 3 == 0) {
        static const char *const values[] = {
            "a", "b", "c", "d", "e", "f", "g", "h",
        };
        strmap_add(request_headers, "x-foo", values[random() % 8]);
    }

    struct strmap *response_headers = strmap_new(tpool, 7);
    strmap_add(response_headers, "content-type", "text/plain");
    strmap_add(response_headers, "x-foo", "bar");
    strmap_add(response_headers, "x-bar", "foo");

    http_cache_heap_put(cache, uri, &info, request_headers,
                        HTTP_STATUS_OK, response_headers,
                        rubber, rubber_id, length);

    pool_rewind(tpool, &mark);
}

/*
 * main
 *
 */

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    static const size_t max_size = 256 * 1024 * 1024;

    struct rubber *rubber = rubber_new(max_size);
    if (rubber == NULL)
        return EXIT_FAILURE;

    struct event_base *event_base = event_init();

    struct pool *pool = pool_new_libc(NULL, "root");
    tpool_init(pool);

    struct pool *pool2 = pool_new_libc(pool, "cache");

    struct http_cache_heap cache;
    http_cache_heap_init(&cache, pool2, max_size);

    for (unsigned i = 0; i < 32 * 1024; ++i)
        put_random(&cache, rubber);

    struct cache_stats stats;
    http_cache_heap_get_stats(&cache, rubber, &stats);
    printf("netto=%zu brutto=%zu ratio=%f\n",
           stats.netto_size, stats.brutto_size,
           (double)stats.netto_size / stats.brutto_size);

    http_cache_heap_deinit(&cache);

    pool_unref(pool2);

    tpool_deinit();
    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);

    rubber_free(rubber);

    return EXIT_SUCCESS;
}
