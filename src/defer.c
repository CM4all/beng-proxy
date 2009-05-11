/*
 * Easy deferral of function calls.  Internally, this uses an event
 * struct with a zero timeout.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "defer.h"
#include "async.h"

#include <event.h>

struct defer {
    pool_t pool;

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
defer_event_callback(int fd __attr_unused, short event __attr_unused, void *ctx)
{
    struct defer *d = ctx;

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
    return (struct defer*)(((char*)ao) - offsetof(struct defer, operation));
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
defer(pool_t pool, defer_callback_t callback, void *ctx,
      struct async_operation_ref *async_ref)
{
    struct defer *d = p_malloc(pool, sizeof(*d));
    struct timeval tv = {
        .tv_sec = 0,
        .tv_usec = 0,
    };

    pool_ref(pool);
    d->pool = pool;
    d->callback = callback;
    d->callback_ctx = ctx;

    if (async_ref != NULL) {
        async_init(&d->operation, &defer_operation);
        async_ref_set(async_ref, &d->operation);
    }

    evtimer_set(&d->event, defer_event_callback, d);
    evtimer_add(&d->event, &tv);
}
