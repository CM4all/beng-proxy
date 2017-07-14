/*
 * Wrapper for lease.h which registers the "event" object as a pool
 * attachment.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PLEASE_H
#define BENG_PROXY_PLEASE_H

#include "lease.hxx"
#include "pool.hxx"

#include <assert.h>

static inline void
p_lease_ref_set(struct lease_ref &lease_ref,
                Lease &lease,
                struct pool &pool, const char *name)
{
    assert(pool_contains(pool, &lease_ref, sizeof(lease_ref)));
    assert(name != nullptr);

    pool_attach_checked(&pool, &lease, name);
    lease_ref.Set(lease);
}

static inline void
p_lease_release(struct lease_ref &lease_ref, bool reuse, struct pool &pool)
{
    pool_detach(&pool, lease_ref.lease);
    lease_ref.Release(reuse);
}

#endif
