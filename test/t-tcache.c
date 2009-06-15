#include "tcache.h"
#include "async.h"
#include "beng-proxy/translation.h"

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

static const struct translate_response response2 = {
    .address = {
        .type = RESOURCE_ADDRESS_LOCAL,
        .u = {
            .local = {
                .path = "/srv/foo/bar.html",
            },
        },
    },
    .base = "/foo/",
    .max_age = -1,
    .user_max_age = -1,
};

static const struct translate_response response3 = {
    .address = {
        .type = RESOURCE_ADDRESS_LOCAL,
        .u = {
            .local = {
                .path = "/srv/foo/index.html",
            },
        },
    },
    .max_age = -1,
    .user_max_age = -1,
};

static const struct translate_response response4 = {
    .address = {
        .type = RESOURCE_ADDRESS_LOCAL,
        .u = {
            .local = {
                .path = "/srv/foo/",
            },
        },
    },
    .max_age = -1,
    .user_max_age = -1,
};

static const uint16_t response5_vary[] = {
    TRANSLATE_QUERY_STRING,
};

static const struct translate_response response5a = {
    .address = {
        .type = RESOURCE_ADDRESS_LOCAL,
        .u = {
            .local = {
                .path = "/src/qs1",
            },
        },
    },
    .max_age = -1,
    .user_max_age = -1,
    .vary = response5_vary,
    .num_vary = sizeof(response5_vary) / sizeof(response5_vary[0]),
};

static const struct translate_response response5b = {
    .address = {
        .type = RESOURCE_ADDRESS_LOCAL,
        .u = {
            .local = {
                .path = "/src/qs2",
            },
        },
    },
    .max_age = -1,
    .user_max_age = -1,
    .vary = response5_vary,
    .num_vary = sizeof(response5_vary) / sizeof(response5_vary[0]),
};

static const uint16_t response5_invalidate[] = {
    TRANSLATE_QUERY_STRING,
};

static const struct translate_response response5c = {
    .address = {
        .type = RESOURCE_ADDRESS_LOCAL,
        .u = {
            .local = {
                .path = "/src/qs3",
            },
        },
    },
    .max_age = -1,
    .user_max_age = -1,
    .vary = response5_vary,
    .num_vary = sizeof(response5_vary) / sizeof(response5_vary[0]),
    .invalidate = response5_invalidate,
    .num_invalidate = sizeof(response5_invalidate) / sizeof(response5_invalidate[0]),
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
    static const struct translate_request request2 = {
        .uri = "/foo/bar.html",
    };
    static const struct translate_request request3 = {
        .uri = "/foo/index.html",
    };
    static const struct translate_request request4 = {
        .uri = "/foo/",
    };
    static const struct translate_request request5 = {
        .uri = "/foo",
    };
    static const struct translate_request request6 = {
        .uri = "/qs",
        .query_string = "abc",
    };
    static const struct translate_request request7 = {
        .uri = "/qs",
        .query_string = "xyz",
    };
    static const struct translate_request request8 = {
        .uri = "/qs/",
        .query_string = "xyz",
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

    next_response = expected_response = &response2;
    translate_cache(pool, cache, &request2, my_callback, NULL, &async_ref);

    next_response = NULL;
    expected_response = &response3;
    translate_cache(pool, cache, &request3, my_callback, NULL, &async_ref);

    expected_response = &response4;
    translate_cache(pool, cache, &request4, my_callback, NULL, &async_ref);

    expected_response = NULL;
    translate_cache(pool, cache, &request5, my_callback, NULL, &async_ref);

    next_response = expected_response = &response5a;
    translate_cache(pool, cache, &request6, my_callback, NULL, &async_ref);

    next_response = expected_response = &response5b;
    translate_cache(pool, cache, &request7, my_callback, NULL, &async_ref);

    next_response = NULL;
    expected_response = &response5a;
    translate_cache(pool, cache, &request6, my_callback, NULL, &async_ref);

    next_response = NULL;
    expected_response = &response5b;
    translate_cache(pool, cache, &request7, my_callback, NULL, &async_ref);

    next_response = expected_response = &response5c;
    translate_cache(pool, cache, &request8, my_callback, NULL, &async_ref);

    next_response = NULL;
    expected_response = &response5a;
    translate_cache(pool, cache, &request6, my_callback, NULL, &async_ref);

    next_response = expected_response = &response5c;
    translate_cache(pool, cache, &request7, my_callback, NULL, &async_ref);

    next_response = expected_response = &response5c;
    translate_cache(pool, cache, &request8, my_callback, NULL, &async_ref);

    expected_response = &response5c;
    translate_cache(pool, cache, &request7, my_callback, NULL, &async_ref);

    /* cleanup */

    translate_cache_close(cache);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
