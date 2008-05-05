#include "widget-registry.h"
#include "async.h"
#include "stock.h"
#include "uri-address.h"
#include "tcache.h"
#include "widget.h"

#include <string.h>

struct data {
    bool got_class;
    const struct widget_class *class;
};

static bool aborted;

static void
widget_class_callback(const struct widget_class *class, void *ctx)
{
    struct data *data = ctx;

    data->got_class = true;
    data->class = class;
}


/*
 * async operation
 *
 */

static void
my_abort(struct async_operation *ao __attr_unused)
{
    aborted = true;
}

static struct async_operation_class my_operation = {
    .abort = my_abort,
};


/*
 * translate.c emulation
 *
 */

static void
my_stock_create(void *ctx __attr_unused, struct stock_item *item __attr_unused,
                const char *uri __attr_unused, void *info __attr_unused,
                struct async_operation_ref *async_ref __attr_unused)
{
}

static bool
my_stock_validate(void *ctx __attr_unused, struct stock_item *item __attr_unused)
{
    return true;
}

static void
my_stock_destroy(void *ctx __attr_unused, struct stock_item *item __attr_unused)
{
}

static struct stock_class my_stock_class = {
    .item_size = sizeof(struct stock_item) + 1,
    .create = my_stock_create,
    .validate = my_stock_validate,
    .destroy = my_stock_destroy,
};


struct stock *
translate_stock_new(pool_t pool, const char *translation_socket __attr_unused)
{
    return stock_new(pool, &my_stock_class, NULL, NULL);
}

void
translate(pool_t pool,
          struct stock *stock __attr_unused,
          const struct translate_request *request,
          translate_callback_t callback,
          void *ctx,
          struct async_operation_ref *async_ref __attr_unused)
{
    assert(request->remote_host == NULL);
    assert(request->host == NULL);
    assert(request->uri == NULL);
    assert(request->widget_type != NULL);
    assert(request->session == NULL);
    assert(request->param == NULL);

    if (strcmp(request->widget_type, "sync") == 0) {
        struct translate_response *response =
            p_calloc(pool, sizeof(*response));
        response->proxy = uri_address_new(pool, "http://foo/");
        callback(response, ctx);
    } else if (strcmp(request->widget_type, "block") == 0) {
        struct async_operation *ao = p_malloc(pool, sizeof(*ao));

        async_init(ao, &my_operation);
        async_ref_set(async_ref, ao);
    } else
        assert(0);
}


/*
 * tests
 *
 */

/** normal run */
static void
test_normal(pool_t pool)
{
    struct data data = {
        .got_class = false,
    };
    struct tcache *tcache;
    struct async_operation_ref async_ref;

    pool = pool_new_linear(pool, "test", 8192);

    tcache = translate_cache_new(pool, translate_stock_new(pool, "/dev/null"));

    aborted = false;
    widget_class_lookup(pool, tcache, "sync",
                        widget_class_callback, &data, &async_ref);
    assert(!aborted);
    assert(data.got_class);
    assert(data.class != NULL);
    assert(strcmp(data.class->address->uri, "http://foo/") == 0);
    assert(data.class->type == WIDGET_TYPE_RAW);
    assert(!data.class->is_container);
    assert(!data.class->old_style);

    pool_unref(pool);

    translate_cache_close(tcache);

    pool_commit();
}

/** caller aborts */
static void
test_abort(pool_t pool)
{
    struct data data = {
        .got_class = false,
    };
    struct tcache *tcache;
    struct async_operation_ref async_ref;

    pool = pool_new_linear(pool, "test", 8192);

    tcache = translate_cache_new(pool, translate_stock_new(pool, "/dev/null"));

    aborted = false;
    widget_class_lookup(pool, tcache,  "block",
                        widget_class_callback, &data, &async_ref);
    assert(!data.got_class);
    assert(!aborted);

    pool_unref(pool);

    async_abort(&async_ref);
    assert(aborted);
    assert(!data.got_class);

    translate_cache_close(tcache);

    pool_commit();
}


/*
 * main
 *
 */

int main(int argc __attr_unused, char **argv __attr_unused) {
    pool_t root_pool;

    root_pool = pool_new_libc(NULL, "root");

    /* run test suite */

    test_normal(root_pool);
    test_abort(root_pool);

    /* cleanup */

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();
}
