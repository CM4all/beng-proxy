/*
 * Temporary memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ROOT_POOL_HXX
#define BENG_PROXY_ROOT_POOL_HXX

#include "pool.hxx"

class RootPool {
    struct pool &p;

public:
    RootPool();
    ~RootPool();

    struct pool &get() {
        return p;
    }

    operator struct pool &() {
        return p;
    }

    operator struct pool *() {
        return &p;
    }
};

#endif
