/*
 * An async_operation implementation which sets a flag.  This can be
 * used by libraries which don't have their own implementation, but
 * need to know whether the operation has been aborted.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_ABORT_FLAG_H
#define BENG_ABORT_FLAG_H

#include "pool.h"
#include "async.h"

struct abort_flag {
    struct async_operation operation;

    bool aborted;
};

void
abort_flag_init(struct abort_flag *af);

static inline void
abort_flag_set(struct abort_flag *af, struct async_operation_ref *async_ref)
{
    abort_flag_init(af);
    async_ref_set(async_ref, &af->operation);
}

#endif
