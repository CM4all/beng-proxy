#include "tcache.h"
#include "async.h"

#include <event.h>

#include <string.h>

static const struct translate_response response1 = {
    .address = {
        .type = RESOURCE_ADDRESS_LOCAL,
        .u = {
            .local = {
                .path = "/var/www/index.html",
            },
        },
    },
    .max_age = -1,
    .user_max_age = -1,
};

const struct translate_response *next_response, *expected_response;

void
translate(__attr_unused pool_t pool,
          __attr_unused struct hstock *tcp_stock,
          __attr_unused const char *socket_path,
          __attr_unused const struct translate_request *request,
          translate_callback_t callback,
          void *ctx,
          __attr_unused struct async_operation_ref *async_ref)
{
    callback(next_response, ctx);
}

static void
my_callback(const struct translate_response *response,
            __attr_unused void *ctx)
{
    if (response == NULL) {
        assert(expected_response == NULL);
    } else {
        assert(expected_response != NULL);
        assert(response->address.type == RESOURCE_ADDRESS_LOCAL);
        assert(expected_response->address.type == RESOURCE_ADDRESS_LOCAL);
        assert(strcmp(response->address.u.local.path,
                      expected_response->address.u.local.path) == 0);
    }
}

int main(int argc __attr_unused, char **argv __attr_unused) {
    void *const tcp_stock = (void *)0x1;
    struct event_base *event_base;
    pool_t pool;
    struct tcache *cache;
    static const struct translate_request request1 = {
        .uri = "/",
    };
    struct async_operation_ref async_ref;

    event_base = event_init();

    pool = pool_new_libc(NULL, "root");

    cache = translate_cache_new(pool, tcp_stock, "/does/not/exist");

    /* test */

    next_response = expected_response = &response1;
    translate_cache(pool, cache, &request1, my_callback, NULL, &async_ref);

    next_response = NULL;
    translate_cache(pool, cache, &request1, my_callback, NULL, &async_ref);

    /* cleanup */

    translate_cache_close(cache);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
