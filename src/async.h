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

#ifndef __BENG_ABORT_H
#define __BENG_ABORT_H

#include "compiler.h"

#include <assert.h>

struct async_operation_class {
    void (*abort)(struct async_operation *ao);
};

struct async_operation {
    struct async_operation_class class;
#ifndef NDEBUG
    int aborted;
#endif
};

static inline void
async_poison(struct async_operation *ao attr_unused)
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
async_abort(struct async_operation *ao)
{
    assert(!ao->aborted);

#ifndef NDEBUG
    ao->aborted = 1;
#endif

    ao->class.abort(ao);
}

#endif
