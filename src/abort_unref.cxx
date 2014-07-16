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

struct UnrefOnAbort {
    struct pool *pool;
    struct async_operation operation;
    struct async_operation_ref ref;

#ifdef TRACE
    const char *file;
    unsigned line;
#endif

    void Abort() {
        ref.Abort();
        pool_unref_fwd(pool);
    }
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
    auto uoa = NewFromPool<UnrefOnAbort>(*pool);

    uoa->pool = pool;
    uoa->operation.Init2<UnrefOnAbort>();
    async_ref->Set(uoa->operation);

#ifdef TRACE
    uoa->file = file;
    uoa->line = line;
#endif

    return &uoa->ref;
}
