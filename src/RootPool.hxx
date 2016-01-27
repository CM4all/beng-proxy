/*
 * Temporary memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ROOT_POOL_HXX
#define BENG_PROXY_ROOT_POOL_HXX

class RootPool {
    struct pool &p;

public:
    RootPool();
    ~RootPool();

    RootPool(const RootPool &) = delete;
    RootPool &operator=(const RootPool &) = delete;

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
