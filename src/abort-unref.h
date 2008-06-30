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

#ifndef __BENG_ABORT_UNREF_H
#define __BENG_ABORT_UNREF_H

#include "pool.h"
#include "trace.h"

struct async_operation_ref;

struct async_operation_ref *
async_unref_on_abort_impl(pool_t pool, struct async_operation_ref *async_ref
                          TRACE_ARGS_DECL);

#define async_unref_on_abort(pool, async_ref) async_unref_on_abort_impl(pool, async_ref TRACE_ARGS)

#endif
