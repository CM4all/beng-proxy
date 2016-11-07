/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ALLOCATOR_PTR_HXX
#define BENG_PROXY_ALLOCATOR_PTR_HXX

#include "pool.hxx"

struct StringView;
template<typename T> struct ConstBuffer;

class AllocatorPtr {
    struct pool &pool;

public:
    AllocatorPtr(struct pool &_pool):pool(_pool) {}

    char *Dup(const char *src) {
        return p_strdup(&pool, src);
    }

    const char *CheckDup(const char *src) {
        return p_strdup_checked(&pool, src);
    }

    template<typename T, typename... Args>
    T *New(Args&&... args) {
        return NewFromPool<T>(pool, std::forward<Args>(args)...);
    }

    template<typename T>
    T *NewArray(size_t n) {
        return PoolAlloc<T>(pool, n);
    }

    void *Dup(const void *data, size_t size) {
        return p_memdup(&pool, data, size);
    }

    ConstBuffer<void> Dup(ConstBuffer<void> src);
    StringView Dup(StringView src);
    const char *DupZ(StringView src);
};

#endif
