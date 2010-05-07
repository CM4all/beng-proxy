/*
 * A wrapper for an async_operation which closes an istream on abort.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "abort-close.h"
#include "async.h"

struct close_on_abort {
    istream_t istream;
    struct async_operation operation;
    struct async_operation_ref ref;
};

/*
 * async operation
 *
 */

static struct close_on_abort *
async_to_coa(struct async_operation *ao)
{
    return (struct close_on_abort*)(((char*)ao) - offsetof(struct close_on_abort, operation));
}

static void
coa_abort(struct async_operation *ao)
{
    struct close_on_abort *coa = async_to_coa(ao);

    async_abort(&coa->ref);
    istream_close(coa->istream);
}

static const struct async_operation_class coa_operation = {
    .abort = coa_abort,
};


/*
 * constructor
 *
 */

struct async_operation_ref *
async_close_on_abort(pool_t pool, istream_t istream,
                     struct async_operation_ref *async_ref)
{
    struct close_on_abort *coa = p_malloc(pool, sizeof(*coa));

    assert(istream != NULL);
    assert(async_ref != NULL);

    coa->istream = istream;
    async_init(&coa->operation, &coa_operation);
    async_ref_set(async_ref, &coa->operation);

    return &coa->ref;
}
