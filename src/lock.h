/*
 * Inter-process synchronization routines.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_LOCK_H
#define __BENG_LOCK_H

#include <inline/poison.h>

#include <assert.h>
#include <stdbool.h>
#include <semaphore.h>

#define LOCK_MAGIC1 (*(const unsigned*)"lck1")
#define LOCK_MAGIC2 (*(const unsigned*)"lck2")

struct lock {
#ifndef NDEBUG
    unsigned magic1;
#endif

    sem_t semaphore;

#ifndef NDEBUG
    unsigned magic2;
#endif
};

static inline void
lock_init(struct lock *lock)
{
    sem_init(&lock->semaphore, 1, 1);

#ifndef NDEBUG
    lock->magic1 = LOCK_MAGIC1;
    lock->magic2 = LOCK_MAGIC2;
#endif
}

static inline void
lock_destroy(struct lock *lock)
{
    assert(lock->magic1 == LOCK_MAGIC1);
    assert(lock->magic2 == LOCK_MAGIC2);

    sem_destroy(&lock->semaphore);

    poison_undefined(lock, sizeof(*lock));
}

static inline void
lock_lock(struct lock *lock)
{
    assert(lock->magic1 == LOCK_MAGIC1);
    assert(lock->magic2 == LOCK_MAGIC2);

    sem_wait(&lock->semaphore);
}

static inline void
lock_unlock(struct lock *lock)
{
    assert(lock->magic1 == LOCK_MAGIC1);
    assert(lock->magic2 == LOCK_MAGIC2);

    sem_post(&lock->semaphore);
}

static inline bool
lock_is_locked(struct lock *lock)
{
    int ret, value;

    assert(lock->magic1 == LOCK_MAGIC1);
    assert(lock->magic2 == LOCK_MAGIC2);

    ret = sem_getvalue(&lock->semaphore, &value);
    return ret == 0 && value <= 0;
}

#endif
