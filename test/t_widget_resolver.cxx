#include "widget_resolver.hxx"
#include "widget_registry.hxx"
#include "async.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "pool.hxx"
#include "RootPool.hxx"
#include "event/Loop.hxx"
#include "util/Cast.hxx"

#include <assert.h>

struct data {
    struct {
        struct async_operation_ref async_ref;

        bool finished;

        /** abort in the callback? */
        bool abort;
    } first, second;

    struct {
        bool requested, finished, aborted;
        struct async_operation operation;
        widget_class_callback_t callback;
        void *ctx;
    } registry;
};

static struct data *global;

const WidgetView *
widget_view_lookup(const WidgetView *view,
                   gcc_unused const char *name)
{
    return view;
}

static void
data_init(struct data *data)
{
    data->first.finished = false;
    data->first.abort = false;
    data->second.finished = false;
    data->second.abort = false;
    data->registry.requested = false;
    data->registry.finished = false;
    data->registry.aborted = false;
    data->registry.callback = nullptr;

    global = data;
}

static void
widget_resolver_callback1(void *ctx)
{
    struct data *data = (struct data *)ctx;

    assert(!data->first.finished);
    assert(!data->second.finished);

    data->first.finished = true;

    if (data->first.abort)
        data->second.async_ref.Abort();
}

static void
widget_resolver_callback2(void *ctx)
{
    struct data *data = (struct data *)ctx;

    assert(data->first.finished);
    assert(!data->second.finished);
    assert(!data->second.abort);

    data->second.finished = true;
}

/*
 * async operation
 *
 */

static struct data *
async_to_data(struct async_operation *ao)
{
    return ContainerCast(ao, struct data, registry.operation);
}

static void
widget_registry_abort(struct async_operation *ao __attr_unused)
{
    struct data *data = async_to_data(ao);

    data->registry.aborted = true;
}

static const struct async_operation_class widget_registry_operation = {
    .abort = widget_registry_abort,
};

/*
 * widget-registry.c emulation
 *
 */

void
widget_class_lookup(gcc_unused struct pool &pool,
                    gcc_unused struct pool &widget_pool,
                    gcc_unused struct tcache &translate_cache,
                    gcc_unused const char *widget_type,
                    widget_class_callback_t callback,
                    void *ctx,
                    struct async_operation_ref &async_ref)
{
    struct data *data = global;
    assert(!data->registry.requested);
    assert(!data->registry.finished);
    assert(!data->registry.aborted);
    assert(data->registry.callback == nullptr);

    data->registry.requested = true;
    data->registry.callback = callback;
    data->registry.ctx = ctx;
    data->registry.operation.Init(widget_registry_operation);
    async_ref.Set(data->registry.operation);
}

static void
widget_registry_finish(struct data *data)
{
    assert(data->registry.requested);
    assert(!data->registry.finished);
    assert(!data->registry.aborted);
    assert(data->registry.callback != nullptr);

    data->registry.finished = true;

    static const WidgetClass cls = WidgetClass(WidgetClass::Root());
    data->registry.callback(&cls, data->registry.ctx);
}


/*
 * tests
 *
 */

static void
test_normal(struct pool *pool)
{
    struct data data;
    data_init(&data);

    pool = pool_new_linear(pool, "test", 8192);

    auto widget = NewFromPool<Widget>(*pool);
    widget->Init(*pool, nullptr);
    widget->class_name = "foo";

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  widget_resolver_callback1, &data,
                  data.first.async_ref);

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(!data.registry.aborted);

    widget_registry_finish(&data);

    assert(data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(data.registry.finished);
    assert(!data.registry.aborted);

    pool_unref(pool);
    pool_commit();
}

static void
test_abort(struct pool *pool)
{
    struct data data;
    data_init(&data);

    pool = pool_new_linear(pool, "test", 8192);

    auto widget = NewFromPool<Widget>(*pool);
    widget->Init(*pool, nullptr);
    widget->class_name = "foo";

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  widget_resolver_callback1, &data,
                  data.first.async_ref);

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(!data.registry.aborted);

    data.first.async_ref.Abort();

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(data.registry.aborted);

    pool_unref(pool);
    pool_commit();
}

static void
test_two_clients(struct pool *pool)
{
    struct data data;
    data_init(&data);

    pool = pool_new_linear(pool, "test", 8192);

    auto widget = NewFromPool<Widget>(*pool);
    widget->Init(*pool, nullptr);
    widget->class_name = "foo";

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  widget_resolver_callback1, &data,
                  data.first.async_ref);

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  widget_resolver_callback2, &data,
                  data.second.async_ref);

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(!data.registry.aborted);

    widget_registry_finish(&data);

    assert(data.first.finished);
    assert(data.second.finished);
    assert(data.registry.requested);
    assert(data.registry.finished);
    assert(!data.registry.aborted);

    pool_unref(pool);
    pool_commit();
}

static void
test_two_abort(struct pool *pool)
{
    struct data data;
    data_init(&data);
    data.first.abort = true;

    pool = pool_new_linear(pool, "test", 8192);

    auto widget = NewFromPool<Widget>(*pool);
    widget->Init(*pool, nullptr);
    widget->class_name = "foo";

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  widget_resolver_callback1, &data,
                  data.first.async_ref);

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  widget_resolver_callback2, &data,
                  data.second.async_ref);

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(!data.registry.aborted);

    widget_registry_finish(&data);

    assert(data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(data.registry.finished);
    assert(!data.registry.aborted);

    pool_unref(pool);
    pool_commit();
}


/*
 * main
 *
 */

int main(int argc __attr_unused, char **argv __attr_unused) {
    EventLoop event_loop;

    /* run test suite */

    test_normal(RootPool());
    test_abort(RootPool());
    test_two_clients(RootPool());
    test_two_abort(RootPool());
}
