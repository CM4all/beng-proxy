/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_LEASE_HXX
#define BENG_PROXY_WAS_LEASE_HXX

#include <stdint.h>

class WasLease {
public:
    virtual void ReleaseWas(bool reuse) = 0;
    virtual void ReleaseWasStop(uint64_t input_received) = 0;
};

#endif
