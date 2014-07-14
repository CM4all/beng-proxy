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

#include "abort_unref.hxx"
#include "async.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"

struct unref_on_abort {
    struct pool *pool;
    struct async_operation operation;
    struct async_operation_ref ref;

#ifdef TRACE
    const char *file;
    unsigned line;
#endif
};

/*
 * async operation
 *
 */

static void
uoa_abort(struct async_operation *ao)
{
    unref_on_abort &uoa = ContainerCast2(*ao, &unref_on_abort::operation);
#ifdef TRACE
    const char *file = uoa.file;
    unsigned line = uoa.line;
#endif

    uoa.ref.Abort();
    pool_unref_fwd(uoa.pool);
}

static const struct async_operation_class uoa_operation = {
    .abort = uoa_abort,
};


/*
 * constructor
 *
 */

struct async_operation_ref *
async_unref_on_abort_impl(struct pool *pool,
                          struct async_operation_ref *async_ref
                          TRACE_ARGS_DECL)
{
    auto uoa = NewFromPool<struct unref_on_abort>(*pool);

    uoa->pool = pool;
    uoa->operation.Init(uoa_operation);
    async_ref->Set(uoa->operation);

#ifdef TRACE
    uoa->file = file;
    uoa->line = line;
#endif

    return &uoa->ref;
}
