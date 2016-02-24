/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_LEASE_HXX
#define BENG_PROXY_WAS_LEASE_HXX

#include <assert.h>

class WasLease {
public:
    virtual void ReleaseWas(bool reuse) = 0;
};

#endif
