/*
 * A wrapper for an async_operation which unrefs the pool on abort.
 *
 * This solves a problem of many libraries which reference a pool, but
 * pass the async_ref object to another library.  When the caller
 * aborts the operation, the "middle" library never gets a chance to
 * unref the pool; plugging this wrapper solves this problem.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "abort-unref.h"
#include "async.h"

struct unref_on_abort {
    pool_t pool;
    struct async_operation operation;
    struct async_operation_ref ref;
};

/*
 * async operation
 *
 */

static struct unref_on_abort *
async_to_uoa(struct async_operation *ao)
{
    return (struct unref_on_abort*)(((char*)ao) - offsetof(struct unref_on_abort, operation));
}

static void
uoa_delayed_abort(struct async_operation *ao)
{
    struct unref_on_abort *uoa = async_to_uoa(ao);

    async_abort(&uoa->ref);
    pool_unref(uoa->pool);
}

static struct async_operation_class uoa_delayed_operation = {
    .abort = uoa_delayed_abort,
};


/*
 * constructor
 *
 */

struct async_operation_ref *
async_unref_on_abort(pool_t pool, struct async_operation_ref *async_ref)
{
    struct unref_on_abort *uoa = p_malloc(pool, sizeof(*uoa));

    uoa->pool = pool;
    async_init(&uoa->operation, &uoa_delayed_operation);
    async_ref_set(async_ref, &uoa->operation);

    return &uoa->ref;
}
