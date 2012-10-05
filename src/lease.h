/*
 * Handler for leases.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_LEASE_H
#define __BENG_LEASE_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

struct lease {
    void (*release)(bool reuse, void *ctx);
};

struct lease_ref {
    const struct lease *lease;

    void *ctx;

#ifndef NDEBUG
    bool released;
#endif
};

static inline void
lease_ref_set(struct lease_ref *lease_ref,
              const struct lease *lease, void *ctx)
{
    assert(lease_ref != NULL);
    assert(lease != NULL);

    lease_ref->lease = lease;
    lease_ref->ctx = ctx;

#ifndef NDEBUG
    lease_ref->released = false;
#endif
}

static inline void
lease_direct_release(const struct lease *lease, void *ctx, bool reuse)
{
    assert(lease != NULL);

    lease->release(reuse, ctx);
}

static inline void
lease_release(struct lease_ref *lease_ref, bool reuse)
{
    assert(lease_ref != NULL);
    assert(lease_ref->lease != NULL);
    assert(!lease_ref->released);

#ifndef NDEBUG
    lease_ref->released = true;
#endif

    lease_direct_release(lease_ref->lease, lease_ref->ctx, reuse);
}

#endif
