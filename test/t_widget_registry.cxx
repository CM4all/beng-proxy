#include "widget-registry.h"
#include "async.h"
#include "stock.h"
#include "http_address.h"
#include "tcache.h"
#include "tstock.h"
#include "translate-client.h"
#include "translate_request.hxx"
#include "translate_response.hxx"
#include "widget.h"
#include "widget-class.h"
#include "transformation.h"

#include <string.h>
#include <event.h>

struct data {
    bool got_class;
    const struct widget_class *cls;
};

static bool aborted;

static void
widget_class_callback(const struct widget_class *cls, void *ctx)
{
    struct data *data = (struct data *)ctx;

    data->got_class = true;
    data->cls = cls;
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
tstock_translate(gcc_unused struct tstock *stock, struct pool *pool,
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
        translate_response *response = NewFromPool<translate_response>(pool);
        response->address.type = RESOURCE_ADDRESS_HTTP;
        response->address.u.http = http_address_parse(pool, "http://foo/", NULL);
        response->views = (widget_view *)p_calloc(pool, sizeof(*response->views));
        response->views->address = response->address;
        handler->response(response, ctx);
    } else if (strcmp(request->widget_type, "block") == 0) {
        async_operation *ao = NewFromPool<async_operation>(pool);

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
test_normal(struct pool *pool)
{
    struct data data = {
        .got_class = false,
    };
    tstock *const translate_stock = (tstock *)0x1;
    struct tcache *tcache;
    struct async_operation_ref async_ref;

    pool = pool_new_linear(pool, "test", 8192);

    tcache = translate_cache_new(pool, translate_stock, 1024);

    aborted = false;
    widget_class_lookup(pool, pool, tcache, "sync",
                        widget_class_callback, &data, &async_ref);
    assert(!aborted);
    assert(data.got_class);
    assert(data.cls != NULL);
    assert(data.cls->views.address.type == RESOURCE_ADDRESS_HTTP);
    assert(data.cls->views.address.u.http->scheme == URI_SCHEME_HTTP);
    assert(strcmp(data.cls->views.address.u.http->host_and_port, "foo") == 0);
    assert(strcmp(data.cls->views.address.u.http->path, "/") == 0);
    assert(data.cls->views.next == NULL);
    assert(data.cls->views.transformation == NULL);

    pool_unref(pool);

    translate_cache_close(tcache);

    pool_commit();
}

/** caller aborts */
static void
test_abort(struct pool *pool)
{
    struct data data = {
        .got_class = false,
    };
    tstock *const translate_stock = (tstock *)0x1;
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
    struct pool *root_pool;

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
