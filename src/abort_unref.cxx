/*
 * A wrapper for an Cancellable which unrefs the pool on abort.
 *
 * This solves a problem of many libraries which reference a pool, but
 * pass the async_ref object to another library.  When the caller
 * aborts the operation, the "middle" library never gets a chance to
 * unref the pool; plugging this wrapper solves this problem.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "abort_unref.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/Cancellable.hxx"

struct UnrefOnAbort final : Cancellable {
    struct pool &pool;
    CancellablePointer cancel_ptr;

#ifdef TRACE
    const char *const file;
    unsigned line;
#endif

    UnrefOnAbort(struct pool &_pool,
                 CancellablePointer &_cancel_ptr
                 TRACE_ARGS_DECL_)
        :pool(_pool)
         TRACE_ARGS_INIT {
        _cancel_ptr = *this;
    }

    /* virtual methods from class Cancellable */
    void Cancel() override {
        cancel_ptr.Cancel();
        pool_unref_fwd(&pool);
    }
};

/*
 * constructor
 *
 */

CancellablePointer &
async_unref_on_abort_impl(struct pool &pool,
                          CancellablePointer &cancel_ptr
                          TRACE_ARGS_DECL)
{
    auto uoa = NewFromPool<UnrefOnAbort>(pool, pool, cancel_ptr
                                         TRACE_ARGS_FWD);
    return uoa->cancel_ptr;
}
