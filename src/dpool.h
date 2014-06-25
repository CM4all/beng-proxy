/*
 * Distributed memory pool in shared memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_DPOOL_H
#define __BENG_DPOOL_H

#include <stddef.h>
#include <stdbool.h>

struct dpool;
struct shm;

#ifdef __cplusplus

#include <utility>
#include <new>

extern "C" {
#endif

/**
 * Create a new memory pool.
 *
 * @return the new dpool object, or NULL if the shm object has no free
 * space
 */
struct dpool *
dpool_new(struct shm *shm);

/**
 * Destroy the memory pool.  All allocated memory is freed.
 */
void
dpool_destroy(struct dpool *pool);

bool
dpool_is_fragmented(const struct dpool *pool);

/**
 * Allocate memory from the pool.
 *
 * @return a pointer to the start, or NULL if allocation failed.
 */
void *
d_malloc(struct dpool *pool, size_t size);

/**
 * Frees the memory previously allocated by d_malloc().
 */
void
d_free(struct dpool *pool, const void *p);

/**
 * Duplicate a chunk of memory, allocating the new pointer from the
 * pool.
 */
char *
d_memdup(struct dpool *pool, const void *src, size_t length);

/**
 * Duplicate a C string, allocating the new pointer from the pool.
 */
char *
d_strdup(struct dpool *pool, const char *src);

/**
 * Duplicate a string, allocating the new pointer from the pool.
 */
char *
d_strndup(struct dpool *pool, const char *src, size_t length);

#ifdef __cplusplus
}

template<typename T, typename... Args>
T *
NewFromPool(struct dpool *pool, Args&&... args)
{
    void *t = d_malloc(pool, sizeof(T));
    if (t == nullptr)
        return nullptr;

    return ::new(t) T(std::forward<Args>(args)...);
}

template<typename T>
void
DeleteFromPool(struct dpool *pool, T *t)
{
    t->~T();
    d_free(pool, t);
}

template<typename T>
void
DeleteDestroyPool(struct dpool &pool, T *t)
{
    t->~T();
    dpool_destroy(&pool);
}

#endif

#endif
