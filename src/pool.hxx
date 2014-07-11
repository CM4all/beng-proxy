/*
 * Memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_POOL_HXX
#define BENG_PROXY_POOL_HXX

#include "pool.h"

#include <utility>
#include <new>

class AutoRewindPool {
    struct pool &pool;
    pool_mark_state mark;

public:
    AutoRewindPool(struct pool &_pool):pool(_pool) {
        pool_mark(&pool, &mark);
    }

    ~AutoRewindPool() {
        pool_rewind(&pool, &mark);
    }
};

template<typename T>
T *
PoolAlloc(pool &p)
{
    return (T *)p_malloc(&p, sizeof(T));
}

template<typename T>
T *
PoolAlloc(pool &p, size_t n)
{
    return (T *)p_malloc(&p, sizeof(T) * n);
}

template<typename T, typename... Args>
T *
NewFromPool(pool &p, Args&&... args)
{
    void *t = PoolAlloc<T>(p);
    return ::new(t) T(std::forward<Args>(args)...);
}

template<typename T>
void
DeleteFromPool(struct pool &pool, T *t)
{
    t->~T();
    p_free(&pool, t);
}

template<typename T>
void
DeleteUnrefPool(struct pool &pool, T *t)
{
    DeleteFromPool(pool, t);
    pool_unref(&pool);
}

template<typename T>
void
DeleteUnrefTrashPool(struct pool &pool, T *t)
{
    pool_trash(&pool);
    DeleteUnrefPool(pool, t);
}

class PoolAllocator {
    struct pool &pool;

public:
    explicit constexpr PoolAllocator(struct pool &_pool):pool(_pool) {}

    void *Allocate(size_t size) {
        return p_malloc(&pool, size);
    }

    char *DupString(const char *p) {
        return p_strdup(&pool, p);
    }

    void Free(void *p) {
        p_free(&pool, p);
    }

    template<typename T, typename... Args>
    T *New(Args&&... args) {
        return NewFromPool<T>(pool, std::forward<Args>(args)...);
    }

    template<typename T>
    void Delete(T *t) {
        DeleteFromPool(pool, t);
    }
};

#endif
