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

static void
widget_class_callback(const struct widget_class *class, void *ctx)
{
    struct data *data = ctx;

    data->got_class = true;
    data->class = class;
}


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

static int
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
    struct translate_response *response;

    assert(request->remote_host == NULL);
    assert(request->host == NULL);
    assert(request->uri == NULL);
    assert(request->widget_type != NULL);
    assert(request->session == NULL);
    assert(request->param == NULL);

    response = p_calloc(pool, sizeof(*response));
    response->proxy = uri_address_new(pool, "http://foo/");

    callback(response, ctx);
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

    widget_class_lookup(pool, tcache,  "foo",
                        widget_class_callback, &data, &async_ref);
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


/*
 * main
 *
 */

int main(int argc __attr_unused, char **argv __attr_unused) {
    pool_t root_pool;

    root_pool = pool_new_libc(NULL, "root");

    /* run test suite */

    test_normal(root_pool);

    /* cleanup */

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();
}
