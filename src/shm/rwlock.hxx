/*
 * Inter-process synchronization routines; rwlock emulation on
 * semaphores.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SHM_RWLOCK_HXX
#define SHM_RWLOCK_HXX

#include "lock.h"

#include <glib.h>

struct ShmRwLock {
    struct lock write;

    /**
     * Counter for the number of readers.
     */
    volatile gint num_readers;
};

static inline void
rwlock_init(ShmRwLock *lock)
{
    lock_init(&lock->write);
    g_atomic_int_set(&lock->num_readers, 0);
}

static inline void
rwlock_destroy(ShmRwLock *lock)
{
    lock_destroy(&lock->write);
}

static inline void
rwlock_rlock(ShmRwLock *lock)
{
    g_atomic_int_inc(&lock->num_readers);
    if (!lock_is_locked(&lock->write))
        /* no writer is waiting - we're done */
        return;

    /* slow route: undo the increment, and retry the increment while
       the write lock is held */

    (void)g_atomic_int_dec_and_test(&lock->num_readers);

    lock_lock(&lock->write);

    assert(g_atomic_int_get(&lock->num_readers) >= 0);
    g_atomic_int_inc(&lock->num_readers);

    lock_unlock(&lock->write);
}

static inline void
rwlock_runlock(ShmRwLock *lock)
{
    assert(g_atomic_int_get(&lock->num_readers) > 0);

    (void)g_atomic_int_dec_and_test(&lock->num_readers);
}

static inline bool
rwlock_is_rlocked(ShmRwLock *lock)
{
    return g_atomic_int_get(&lock->num_readers) > 0;
}

static inline void
rwlock_wlock(ShmRwLock *lock)
{
    lock_lock(&lock->write);

    /* wait for all readers to finish; new readers cannot appear,
       because lock->write is locked */

    while (rwlock_is_rlocked(lock))
        g_usleep(1);
}

static inline void
rwlock_wunlock(ShmRwLock *lock)
{
    lock_unlock(&lock->write);
}

static inline bool
rwlock_is_wlocked(ShmRwLock *lock)
{
    return lock_is_locked(&lock->write);
}

#endif
