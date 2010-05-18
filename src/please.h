/*
 * Wrapper for lease.h which registers the "event" object as a pool
 * attachment.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PLEASE_H
#define BENG_PROXY_PLEASE_H

#include "lease.h"
#include "pool.h"

#include <assert.h>

static inline void
p_lease_ref_set(struct lease_ref *lease_ref,
                const struct lease *lease, void *ctx,
                pool_t pool, const char *name)
{
    assert(lease_ref != NULL);
    assert(lease != NULL);
    assert(pool != NULL);
    assert(pool_contains(pool, lease_ref, sizeof(*lease_ref)));
    assert(name != NULL);

    pool_attach_checked(pool, ctx, name);
    lease_ref_set(lease_ref, lease, ctx);
}

static inline void
p_lease_release(struct lease_ref *lease_ref, bool reuse, pool_t pool)
{
    assert(lease_ref != NULL);
    assert(pool != NULL);

    pool_detach(pool, lease_ref->ctx);
    lease_release(lease_ref, reuse);
}

#endif
