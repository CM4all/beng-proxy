#include "widget-registry.h"
#include "async.h"
#include "stock.h"
#include "uri-address.h"
#include "tcp-stock.h"
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

void
translate(pool_t pool,
          struct hstock *tcp_stock __attr_unused, const char *socket_path __attr_unused,
          const struct translate_request *request,
          translate_callback_t callback,
          void *ctx,
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

    tcache = translate_cache_new(pool, (struct hstock *)(size_t)1, "/dev/null");

    aborted = false;
    widget_class_lookup(pool, pool, tcache, "sync",
                        widget_class_callback, &data, &async_ref);
    assert(!aborted);
    assert(data.got_class);
    assert(data.class != NULL);
    assert(data.class->address.type == RESOURCE_ADDRESS_HTTP);
    assert(strcmp(data.class->address.u.http->uri, "http://foo/") == 0);
    assert(data.class->type == WIDGET_TYPE_RAW);
    assert(!data.class->is_container);

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

    tcache = translate_cache_new(pool, (struct hstock *)(size_t)1, "/dev/null");

    aborted = false;
    widget_class_lookup(pool, pool, tcache,  "block",
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
