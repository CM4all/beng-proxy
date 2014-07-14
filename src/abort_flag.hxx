/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_ABORT_FLAG_HXX
#define BENG_ABORT_FLAG_HXX

#include "async.hxx"

/**
 * An async_operation implementation which sets a flag.  This can be
 * used by libraries which don't have their own implementation, but
 * need to know whether the operation has been aborted.
 */
struct abort_flag {
    struct async_operation operation;

    bool aborted;
};

/**
 * Initialize the abort_flag object, which was allocated by the
 * caller.
 */
void
abort_flag_init(struct abort_flag *af);

/**
 * Initialize the abort_flag object, and register it with the
 * #async_operation_ref object.
 */
static inline void
abort_flag_set(struct abort_flag *af, struct async_operation_ref *async_ref)
{
    abort_flag_init(af);
    async_ref_set(async_ref, &af->operation);
}

#endif
