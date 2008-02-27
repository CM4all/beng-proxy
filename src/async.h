/*
 * API for aborting asynchronous operations.
 *
 * The idea behind it is that functions starting an asynchronous
 * operation return a pointer to a struct async_operation, which can
 * be used to call async_abort().  If the operation happened to be
 * completed before the asynchronous function returns, it will return
 * NULL.
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
 * can be used to abort the operation by calling async_abort().  In
 * this case, the callback is not invoked.
 *
 */

#ifndef __BENG_ABORT_H
#define __BENG_ABORT_H

#include <inline/compiler.h>

#include <assert.h>
#include <stddef.h>

struct async_operation;

struct async_operation_class {
    void (*abort)(struct async_operation *ao);
};

struct async_operation {
    struct async_operation_class class;
#ifndef NDEBUG
    int aborted;
#endif
};

struct async_operation_ref {
    struct async_operation *operation;
};

static inline void
async_poison(struct async_operation *ao __attr_unused)
{
#ifndef NDEBUG
    ao->aborted = -1;
#endif
}

static inline void
async_init(struct async_operation *ao,
           const struct async_operation_class *class)
{
    ao->class = *class;
#ifndef NDEBUG
    ao->aborted = 0;
#endif
}

static inline void
async_ref_clear(struct async_operation_ref *ref)
{
    assert(ref != NULL);

    ref->operation = NULL;
}

static inline int
async_ref_defined(const struct async_operation_ref *ref)
{
    assert(ref != NULL);

    return ref->operation != NULL;
}

static inline void
async_ref_poison(struct async_operation_ref *ref __attr_unused)
{
    assert(ref != NULL);

#ifndef NDEBUG
    ref->operation = (struct async_operation *)0x03030303;
#endif
}

static inline void
async_ref_set(struct async_operation_ref *ref,
              struct async_operation *ao)
{
    assert(ref != NULL);
    assert(ao != NULL);
    assert(!ao->aborted);

    ref->operation = ao;
}

static inline void
async_operation_abort(struct async_operation *ao)
{
    assert(ao != NULL);
    assert(!ao->aborted);

#ifndef NDEBUG
    ao->aborted = 1;
#endif

    ao->class.abort(ao);
}

static inline void
async_abort(struct async_operation_ref *ref)
{
    struct async_operation *ao;

    assert(ref != NULL);

    ao = ref->operation;
#ifndef NDEBUG
    ref->operation = NULL;
#endif

    async_operation_abort(ao);
}

#endif
