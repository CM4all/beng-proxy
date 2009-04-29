/*
 * An async_operation implementation which sets a flag.  This can be
 * used by libraries which don't have their own implementation, but
 * need to know whether the operation has been aborted.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "abort-flag.h"

/*
 * async operation
 *
 */

static struct abort_flag *
async_to_af(struct async_operation *ao)
{
    return (struct abort_flag*)(((char*)ao) - offsetof(struct abort_flag, operation));
}

static void
af_abort(struct async_operation *ao)
{
    struct abort_flag *af = async_to_af(ao);

    assert(!af->aborted);

    af->aborted = true;
}

static const struct async_operation_class abort_flag_operation = {
    .abort = af_abort,
};


/*
 * constructor
 *
 */

void
abort_flag_init(struct abort_flag *af)
{
    async_init(&af->operation, &abort_flag_operation);
    af->aborted = false;
}
