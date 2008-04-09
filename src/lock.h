/*
 * Inter-process synchronization routines.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_LOCK_H
#define __BENG_LOCK_H

#include <semaphore.h>

struct lock {
    sem_t semaphore;
};

static inline void
lock_init(struct lock *lock)
{
    sem_init(&lock->semaphore, 1, 1);
}

static inline void
lock_destroy(struct lock *lock)
{
    sem_destroy(&lock->semaphore);
}

static inline void
lock_lock(struct lock *lock)
{
    sem_wait(&lock->semaphore);
}

static inline void
lock_unlock(struct lock *lock)
{
    sem_post(&lock->semaphore);
}

#endif
