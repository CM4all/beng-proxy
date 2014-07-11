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
    struct pool *const pool;
    pool_mark_state mark;

public:
    AutoRewindPool(struct pool *_pool):pool(_pool) {
        pool_mark(pool, &mark);
    }

    ~AutoRewindPool() {
        pool_rewind(pool, &mark);
    }
};

template<typename T>
T *
PoolAlloc(pool *p)
{
    return (T *)p_malloc(p, sizeof(T));
}

template<typename T>
T *
PoolAlloc(pool *p, size_t n)
{
    return (T *)p_malloc(p, sizeof(T) * n);
}

template<typename T, typename... Args>
T *
NewFromPool(pool *p, Args&&... args)
{
    void *t = PoolAlloc<T>(p);
    return ::new(t) T(std::forward<Args>(args)...);
}

template<typename T>
void
DeleteFromPool(struct pool *pool, T *t)
{
    t->~T();
    p_free(pool, t);
}

template<typename T>
void
DeleteUnrefPool(struct pool &pool, T *t)
{
    DeleteFromPool(&pool, t);
    pool_unref(&pool);
}

template<typename T>
void
DeleteUnrefTrashPool(struct pool &pool, T *t)
{
    pool_trash(&pool);
    DeleteUnrefPool(pool, t);
}

#endif
