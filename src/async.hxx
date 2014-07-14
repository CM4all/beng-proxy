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

#include <inline/compiler.h>

#include <assert.h>

struct async_operation;

struct async_operation_class {
    void (*abort)(struct async_operation *ao);
};

struct async_operation {
    struct async_operation_class cls;

#ifndef NDEBUG
    bool finished;

    bool aborted;
#endif

    void Init(const struct async_operation_class &_cls) {
        cls = _cls;

#ifndef NDEBUG
        finished = false;
        aborted = false;
#endif
    }

    void Abort() {
        assert(!finished);
        assert(!aborted);

#ifndef NDEBUG
        aborted = true;
#endif

        cls.abort(this);
    }

    /**
     * Mark this operation as "finished".  This is a no-op in the
     * NDEBUG build.
     */
    void Finished() {
        assert(!finished);
        assert(!aborted);

#ifndef NDEBUG
        finished = true;
#endif
    }
};

struct async_operation_ref {
    struct async_operation *operation;

#ifndef NDEBUG
    /**
     * A copy of the "operation" pointer, for post-mortem debugging.
     */
    struct async_operation *copy;
#endif

    constexpr bool IsDefined() const {
        return operation != nullptr;
    }

    void Clear() {
        operation = nullptr;
    }

    void Poison() {
#ifndef NDEBUG
        operation = (struct async_operation *)0x03030303;
#endif
    }

    void Set(struct async_operation &ao) {
        assert(!ao.finished);
        assert(!ao.aborted);

        operation = &ao;
#ifndef NDEBUG
        copy = &ao;
#endif
    }

    void Abort() {
        assert(operation != nullptr);
        assert(operation == copy);

        struct async_operation &ao = *operation;
#ifndef NDEBUG
        operation = nullptr;
#endif

        ao.Abort();
    }
};

#endif
