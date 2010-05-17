/*
 * Handler for leases.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_LEASE_H
#define __BENG_LEASE_H

#include <inline/poison.h>

#include <assert.h>

struct lease {
    void (*release)(bool reuse, void *ctx);
};

struct lease_ref {
    const struct lease *lease;

    void *ctx;
};

static inline void
lease_ref_set(struct lease_ref *lease_ref,
              const struct lease *lease, void *ctx)
{
    assert(lease_ref != NULL);
    assert(lease != NULL);

    lease_ref->lease = lease;
    lease_ref->ctx = ctx;
}

static inline void
lease_ref_poison(struct lease_ref *lease_ref)
{
    assert(lease_ref != NULL);

    poison_undefined(lease_ref, sizeof(*lease_ref));
}

static inline void
lease_direct_release(const struct lease *lease, void *ctx, bool reuse)
{
    lease->release(reuse, ctx);
}

static inline void
lease_release(struct lease_ref *lease_ref, bool reuse)
{
    assert(lease_ref != NULL);
    assert(lease_ref->lease != NULL);

    lease_direct_release(lease_ref->lease, lease_ref->ctx, reuse);
    lease_ref_poison(lease_ref);
}

#endif
