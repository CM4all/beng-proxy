/*
 * A wrapper for an async_operation which closes an istream on abort.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "abort_close.hxx"
#include "async.hxx"
#include "pool.hxx"
#include "istream.h"
#include "util/Cast.hxx"

struct close_on_abort {
    struct istream *istream;
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
    return ContainerCast(ao, struct close_on_abort, operation);
}

static void
coa_abort(struct async_operation *ao)
{
    struct close_on_abort *coa = async_to_coa(ao);

    async_abort(&coa->ref);
    istream_close_unused(coa->istream);
}

static const struct async_operation_class coa_operation = {
    .abort = coa_abort,
};


/*
 * constructor
 *
 */

struct async_operation_ref *
async_close_on_abort(struct pool *pool, struct istream *istream,
                     struct async_operation_ref *async_ref)
{
    auto coa = NewFromPool<struct close_on_abort>(*pool);

    assert(istream != nullptr);
    assert(!istream_has_handler(istream));
    assert(async_ref != nullptr);

    coa->istream = istream;
    async_init(&coa->operation, &coa_operation);
    async_ref_set(async_ref, &coa->operation);

    return &coa->ref;
}

struct async_operation_ref *
async_optional_close_on_abort(struct pool *pool, struct istream *istream,
                              struct async_operation_ref *async_ref)
{
    return istream != nullptr
        ? async_close_on_abort(pool, istream, async_ref)
        : async_ref;
}
