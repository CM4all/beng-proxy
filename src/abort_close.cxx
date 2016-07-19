/*
 * A wrapper for an async_operation which closes an istream on abort.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "abort_close.hxx"
#include "pool.hxx"
#include "istream/istream.hxx"
#include "util/Cast.hxx"
#include "util/Cancellable.hxx"

struct CloseOnAbort final : Cancellable {
    Istream &istream;
    CancellablePointer cancel_ptr;

    CloseOnAbort(Istream &_istream,
                 CancellablePointer &_cancel_ptr)
        :istream(_istream) {
        _cancel_ptr = *this;
    }

    /* virtual methods from class Cancellable */
    void Cancel() override {
        cancel_ptr.Cancel();
        istream.CloseUnused();
    }
};

/*
 * constructor
 *
 */

CancellablePointer &
async_close_on_abort(struct pool &pool, Istream &istream,
                     CancellablePointer &cancel_ptr)
{
    assert(!istream.HasHandler());

    auto coa = NewFromPool<struct CloseOnAbort>(pool, istream, cancel_ptr);
    return coa->cancel_ptr;
}

CancellablePointer &
async_optional_close_on_abort(struct pool &pool, Istream *istream,
                              CancellablePointer &cancel_ptr)
{
    return istream != nullptr
        ? async_close_on_abort(pool, *istream, cancel_ptr)
        : cancel_ptr;
}
