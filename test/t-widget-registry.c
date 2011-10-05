#include "widget-registry.h"
#include "async.h"
#include "stock.h"
#include "uri-address.h"
#include "tcache.h"
#include "tstock.h"
#include "translate.h"
#include "widget.h"
#include "widget-class.h"
#include "transformation.h"

#include <string.h>
#include <event.h>

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
my_abort(gcc_unused struct async_operation *ao)
{
    aborted = true;
}

static const struct async_operation_class my_operation = {
    .abort = my_abort,
};


/*
 * tstock.c emulation
 *
 */

void
tstock_translate(gcc_unused struct tstock *stock, pool_t pool,
                 const struct translate_request *request,
                 const struct translate_handler *handler, void *ctx,
                 struct async_operation_ref *async_ref)
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
        response->address.type = RESOURCE_ADDRESS_HTTP;
        response->address.u.http = uri_address_new(pool, "http://foo/");
        response->views = p_calloc(pool, sizeof(*response->views));
        response->views->address = response->address;
        handler->response(response, ctx);
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
    struct tstock *const translate_stock = (void *)0x1;
    struct tcache *tcache;
    struct async_operation_ref async_ref;

    pool = pool_new_linear(pool, "test", 8192);

    tcache = translate_cache_new(pool, translate_stock, 1024);

    aborted = false;
    widget_class_lookup(pool, pool, tcache, "sync",
                        widget_class_callback, &data, &async_ref);
    assert(!aborted);
    assert(data.got_class);
    assert(data.class != NULL);
    assert(data.class->views.address.type == RESOURCE_ADDRESS_HTTP);
    assert(strcmp(data.class->views.address.u.http->uri, "http://foo/") == 0);
    assert(data.class->views.next == NULL);
    assert(data.class->views.transformation == NULL);

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
    struct tstock *const translate_stock = (void *)0x1;
    struct tcache *tcache;
    struct async_operation_ref async_ref;

    pool = pool_new_linear(pool, "test", 8192);

    tcache = translate_cache_new(pool, translate_stock, 1024);

    aborted = false;
    widget_class_lookup(pool, pool, tcache,  "block",
                        widget_class_callback, &data, &async_ref);
    assert(!data.got_class);
    assert(!aborted);

    async_abort(&async_ref);

    /* need to unref the pool after aborted(), because our fake
       tstock_translate() implementation does not reference the
       pool */
    pool_unref(pool);

    assert(aborted);
    assert(!data.got_class);

    translate_cache_close(tcache);

    pool_commit();
}


/*
 * main
 *
 */

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    struct event_base *event_base;
    pool_t root_pool;

    event_base = event_init();

    root_pool = pool_new_libc(NULL, "root");

    /* run test suite */

    test_normal(root_pool);
    test_abort(root_pool);

    /* cleanup */

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    event_base_free(event_base);
}
