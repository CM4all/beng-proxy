/*
 * Distributed memory pool in shared memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SHM_DPOOL_HXX
#define SHM_DPOOL_HXX

#include <stddef.h>

#include <utility>
#include <new>

struct dpool;
struct shm;
struct StringView;

/**
 * Create a new memory pool.
 *
 * @return the new dpool object, or NULL if the shm object has no free
 * space
 */
struct dpool *
dpool_new(struct shm &shm);

/**
 * Destroy the memory pool.  All allocated memory is freed.
 */
void
dpool_destroy(struct dpool *pool);

bool
dpool_is_fragmented(const struct dpool &pool);

/**
 * Allocate memory from the pool.
 *
 * @return a pointer to the start, or NULL if allocation failed.
 */
void *
d_malloc(struct dpool *pool, size_t size)
    throw(std::bad_alloc);

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

char *
d_strdup(struct dpool *pool, StringView src);

static inline char *
d_strdup_checked(struct dpool *pool, const char *src)
{
    return src != NULL ? d_strdup(pool, src) : NULL;
}

/**
 * Duplicate a string, allocating the new pointer from the pool.
 */
char *
d_strndup(struct dpool *pool, const char *src, size_t length);

/**
 * Duplicate data in the given #StringView.  If src.IsNull(), then
 * nullptr is returned; if src.IsEmpty(), then an empty string literal
 * is returned.
 */
StringView
DupStringView(struct dpool &pool, StringView src)
    throw(std::bad_alloc);

/**
 * Free the data allocated by the given #StringView.  It is a no-op if
 * src.IsEmpty() because then it's either nullptr or the empty string
 * literal.
 */
void
FreeStringView(struct dpool &pool, StringView s);

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
