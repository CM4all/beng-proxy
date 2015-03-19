/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SHM_RWLOCK_HXX
#define SHM_RWLOCK_HXX

#include "lock.h"

#include <glib.h>

/**
 * A reader/writer lock emulation using a semaphore.
 */
class ShmRwLock {
    struct lock write;

    /**
     * Counter for the number of readers.
     */
    volatile gint num_readers;

public:
    ShmRwLock() {
        lock_init(&write);
        g_atomic_int_set(&num_readers, 0);
    }

    ~ShmRwLock() {
        lock_destroy(&write);
    }

    void ReadLock() {
        g_atomic_int_inc(&num_readers);
        if (!lock_is_locked(&write))
            /* no writer is waiting - we're done */
            return;

        /* slow route: undo the increment, and retry the increment while
           the write lock is held */

        (void)g_atomic_int_dec_and_test(&num_readers);

        lock_lock(&write);

        assert(g_atomic_int_get(&num_readers) >= 0);
        g_atomic_int_inc(&num_readers);

        lock_unlock(&write);
    }

    void ReadUnlock() {
        assert(g_atomic_int_get(&num_readers) > 0);

        (void)g_atomic_int_dec_and_test(&num_readers);
    }

    gcc_pure
    bool IsReadLocked() const {
        return g_atomic_int_get(&num_readers) > 0;
    }

    void WriteLock() {
        lock_lock(&write);

        /* wait for all readers to finish; new readers cannot appear,
           because write is locked */

        while (IsReadLocked())
            g_usleep(1);
    }

    void WriteUnlock() {
        lock_unlock(&write);
    }

    gcc_pure
    bool IsWriteLocked() {
        return lock_is_locked(&write);
    }
};

#endif
