/*
 * Handler for leases.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LEASE_HXX
#define BENG_PROXY_LEASE_HXX

#include <assert.h>

struct lease {
    void (*release)(bool reuse, void *ctx);

    void Release(void *ctx, bool reuse) const {
        release(reuse, ctx);
    }
};

struct lease_ref {
    const struct lease *lease;

    void *ctx;

#ifndef NDEBUG
    bool released;
#endif

    void Set(const struct lease &_lease, void *_ctx) {
        lease = &_lease;
        ctx = _ctx;

#ifndef NDEBUG
        released = false;
#endif
    }

    void Release(bool reuse) {
        assert(lease != nullptr);
        assert(!released);

#ifndef NDEBUG
        released = true;
#endif

        lease->Release(ctx, reuse);
    }
};

#endif
