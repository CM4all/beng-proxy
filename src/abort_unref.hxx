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

#ifndef BENG_PROXY_ABORT_UNREF_HXX
#define BENG_PROXY_ABORT_UNREF_HXX

#include "trace.h"

struct pool;
struct async_operation_ref;

struct async_operation_ref *
async_unref_on_abort_impl(struct pool *pool,
                          struct async_operation_ref *async_ref
                          TRACE_ARGS_DECL);

#define async_unref_on_abort(pool, async_ref) async_unref_on_abort_impl(pool, async_ref TRACE_ARGS)

#endif
