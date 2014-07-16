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
struct AbortFlag {
    struct async_operation operation;

    bool aborted = false;

    AbortFlag(async_operation_ref &async_ref) {
        operation.Init2<AbortFlag>();
        async_ref.Set(operation);
    }

    void Abort() {
        assert(!aborted);

        aborted = true;
    }
};

#endif
