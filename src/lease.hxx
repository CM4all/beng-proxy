/*
 * Handler for leases.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LEASE_HXX
#define BENG_PROXY_LEASE_HXX

#include <assert.h>

class Lease {
public:
    virtual void ReleaseLease(bool reuse) = 0;
};

struct lease_ref {
    Lease *lease;

#ifndef NDEBUG
    bool released;
#endif

    void Set(Lease &_lease) {
        lease = &_lease;

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

        lease->ReleaseLease(reuse);
    }
};

#endif
