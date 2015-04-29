/*
 * Shared memory for sharing data between worker processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SHM_HXX
#define SHM_HXX

#include <stddef.h>

struct shm;

#include <utility>
#include <new>

struct shm *
shm_new(size_t page_size, unsigned num_pages);

void
shm_ref(struct shm *shm);

void
shm_close(struct shm *shm);

size_t
shm_page_size(const struct shm *shm);

void *
shm_alloc(struct shm *shm, unsigned num_pages);

void
shm_free(struct shm *shm, const void *p);

template<typename T, typename... Args>
T *
NewFromShm(struct shm *shm, unsigned num_pages, Args&&... args)
{
    void *t = shm_alloc(shm, num_pages);
    if (t == nullptr)
        return nullptr;

    return ::new(t) T(std::forward<Args>(args)...);
}

template<typename T>
void
DeleteFromShm(struct shm *shm, T *t)
{
    t->~T();
    shm_free(shm, t);
}

#endif
