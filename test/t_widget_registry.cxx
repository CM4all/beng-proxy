#include "widget_registry.hxx"
#include "async.hxx"
#include "http_address.hxx"
#include "tcache.hxx"
#include "tstock.hxx"
#include "TranslateHandler.hxx"
#include "translate_request.hxx"
#include "translate_response.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "transformation.hxx"
#include "pool.hxx"
#include "RootPool.hxx"
#include "event/Loop.hxx"

#include <string.h>

struct data {
    bool got_class;
    const WidgetClass *cls;
};

static bool aborted;

static void
widget_class_callback(const WidgetClass *cls, void *ctx)
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
tstock_translate(gcc_unused TranslateStock &stock, struct pool &pool,
                 const TranslateRequest &request,
                 const TranslateHandler &handler, void *ctx,
                 struct async_operation_ref &async_ref)
{
    assert(request.remote_host == NULL);
    assert(request.host == NULL);
    assert(request.uri == NULL);
    assert(request.widget_type != NULL);
    assert(request.session.IsNull());
    assert(request.param == NULL);

    if (strcmp(request.widget_type, "sync") == 0) {
        auto response = NewFromPool<TranslateResponse>(pool);
        response->address = ResourceAddress(ResourceAddress::Type::HTTP,
                                            *http_address_parse(&pool, "http://foo/", nullptr));
        response->views = NewFromPool<WidgetView>(pool);
        response->views->Init(nullptr);
        response->views->address = response->address;
        handler.response(*response, ctx);
    } else if (strcmp(request.widget_type, "block") == 0) {
        async_operation *ao = NewFromPool<async_operation>(pool);

        ao->Init(my_operation);
        async_ref.Set(*ao);
    } else
        assert(0);
}


/*
 * tests
 *
 */

/** normal run */
static void
test_normal(struct pool *pool, EventLoop &event_loop)
{
    struct data data = {
        .got_class = false,
    };
    const auto translate_stock = (TranslateStock *)0x1;
    struct async_operation_ref async_ref;

    pool = pool_new_linear(pool, "test", 8192);

    auto *tcache = translate_cache_new(*pool, event_loop,
                                       *translate_stock, 1024);

    aborted = false;
    widget_class_lookup(*pool, *pool, *tcache, "sync",
                        widget_class_callback, &data, async_ref);
    assert(!aborted);
    assert(data.got_class);
    assert(data.cls != NULL);
    assert(data.cls->views.address.type == ResourceAddress::Type::HTTP);
    assert(data.cls->views.address.GetHttp().protocol == HttpAddress::Protocol::HTTP);
    assert(strcmp(data.cls->views.address.GetHttp().host_and_port, "foo") == 0);
    assert(strcmp(data.cls->views.address.GetHttp().path, "/") == 0);
    assert(data.cls->views.next == NULL);
    assert(data.cls->views.transformation == NULL);

    pool_unref(pool);

    translate_cache_close(tcache);

    pool_commit();
}

/** caller aborts */
static void
test_abort(struct pool *pool, EventLoop &event_loop)
{
    struct data data = {
        .got_class = false,
    };
    const auto translate_stock = (TranslateStock *)0x1;
    struct async_operation_ref async_ref;

    pool = pool_new_linear(pool, "test", 8192);

    auto *tcache = translate_cache_new(*pool, event_loop,
                                       *translate_stock, 1024);

    aborted = false;
    widget_class_lookup(*pool, *pool, *tcache,  "block",
                        widget_class_callback, &data, async_ref);
    assert(!data.got_class);
    assert(!aborted);

    async_ref.Abort();

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
    EventLoop event_loop;
    RootPool root_pool;

    test_normal(root_pool, event_loop);
    test_abort(root_pool, event_loop);
}
