/*
 * A wrapper for an async_operation which closes an istream on abort.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "abort_close.hxx"
#include "async.hxx"
#include "pool.hxx"
#include "istream.h"
#include "util/Cast.hxx"

struct CloseOnAbort {
    struct istream *istream;
    struct async_operation operation;
    struct async_operation_ref ref;

    void Abort() {
        ref.Abort();
        istream_close_unused(istream);
    }
};

/*
 * constructor
 *
 */

struct async_operation_ref *
async_close_on_abort(struct pool *pool, struct istream *istream,
                     struct async_operation_ref *async_ref)
{
    auto coa = NewFromPool<struct CloseOnAbort>(*pool);

    assert(istream != nullptr);
    assert(!istream_has_handler(istream));
    assert(async_ref != nullptr);

    coa->istream = istream;
    coa->operation.Init2<CloseOnAbort>();
    async_ref->Set(coa->operation);

    return &coa->ref;
}

struct async_operation_ref *
async_optional_close_on_abort(struct pool *pool, struct istream *istream,
                              struct async_operation_ref *async_ref)
{
    return istream != nullptr
        ? async_close_on_abort(pool, istream, async_ref)
        : async_ref;
}
