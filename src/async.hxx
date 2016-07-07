/*
 * API for aborting asynchronous operations.
 *
 * The idea behind it is that functions starting an asynchronous
 * operation return a pointer to a struct async_operation, which can
 * be used to call async_operation_ref::Abort().
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

/*
 * How to implement and use it:
 *
 * The code starts an asynchronous operation by calling a C function.
 * It passes an operation specific callback function and a pointer to
 * a struct async_operation_ref.
 *
 * When the operation completes (either success or failure), the
 * callback is invoked (note that the callback may be invoked before
 * the function which initiated the operation returns).  The callback
 * is invoked exactly once.
 *
 * There is one exception to this rule: the async_operation_ref struct
 * can be used to abort the operation by calling
 * async_operation_ref::Abort().  In this case, the callback is not
 * invoked.
 *
 */

#ifndef ASYNC_HXX
#define ASYNC_HXX

#include "util/Cancellable.hxx"

struct async_operation_ref : CancellablePointer {
    using CancellablePointer::operator=;

    constexpr bool IsDefined() const {
        return *this;
    }

    void Clear() {
        *this = nullptr;
    }

    void Poison() {
    }

    void Abort() {
        Cancel();
    }

    void AbortAndClear() {
        CancelAndClear();
    }
};

#endif
