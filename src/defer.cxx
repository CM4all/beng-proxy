/*
 * Easy deferral of function calls.  Internally, this uses an event
 * struct with a zero timeout.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "defer.hxx"
#include "async.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <event.h>

struct defer {
    struct pool *pool;

    defer_callback_t callback;
    void *callback_ctx;

    struct async_operation operation;
    struct event event;
};

struct async_operation_ref;


/*
 * libevent callback
 *
 */

static void
defer_event_callback(int fd gcc_unused, short event gcc_unused, void *ctx)
{
    struct defer *d = (struct defer *)ctx;

    d->operation.Finished();

    d->callback(d->callback_ctx);

    pool_unref(d->pool);
    pool_commit();
}


/*
 * async operation
 *
 */

static struct defer *
async_to_defer(struct async_operation *ao)
{
    return ContainerCast(ao, struct defer, operation);
}

static void
defer_abort(struct async_operation *ao)
{
    struct defer *d = async_to_defer(ao);

    event_del(&d->event);
    pool_unref(d->pool);
}

static const struct async_operation_class defer_operation = {
    .abort = defer_abort,
};


/*
 * constructor
 *
 */

void
defer(struct pool *pool, defer_callback_t callback, void *ctx,
      struct async_operation_ref *async_ref)
{
    auto d = NewFromPool<struct defer>(*pool);
    static const struct timeval tv = {
        .tv_sec = 0,
        .tv_usec = 0,
    };

    pool_ref(pool);
    d->pool = pool;
    d->callback = callback;
    d->callback_ctx = ctx;

    if (async_ref != nullptr) {
        d->operation.Init(defer_operation);
        async_ref->Set(d->operation);
    }

    evtimer_set(&d->event, defer_event_callback, d);
    evtimer_add(&d->event, &tv);
}
