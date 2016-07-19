/*
 * A wrapper for an async_operation which closes an istream on abort.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "abort_close.hxx"
#include "async.hxx"
#include "pool.hxx"
#include "istream/istream.hxx"
#include "util/Cast.hxx"

struct CloseOnAbort final : Cancellable {
    Istream &istream;
    struct async_operation_ref ref;

    CloseOnAbort(Istream &_istream,
                 CancellablePointer &_cancel_ptr)
        :istream(_istream) {
        _cancel_ptr = *this;
    }

    /* virtual methods from class Cancellable */
    void Cancel() override {
        ref.Abort();
        istream.CloseUnused();
    }
};

/*
 * constructor
 *
 */

struct async_operation_ref &
async_close_on_abort(struct pool &pool, Istream &istream,
                     CancellablePointer &cancel_ptr)
{
    assert(!istream.HasHandler());

    auto coa = NewFromPool<struct CloseOnAbort>(pool, istream, cancel_ptr);
    return coa->ref;
}

struct async_operation_ref &
async_optional_close_on_abort(struct pool &pool, Istream *istream,
                              struct async_operation_ref &async_ref)
{
    return istream != nullptr
        ? async_close_on_abort(pool, *istream, async_ref)
        : async_ref;
}
