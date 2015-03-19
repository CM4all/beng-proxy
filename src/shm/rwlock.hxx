/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SHM_RWLOCK_HXX
#define SHM_RWLOCK_HXX

#include "lock.h"

#include <atomic>

#include <unistd.h>

/**
 * A reader/writer lock emulation using a semaphore.
 */
class ShmRwLock {
    struct lock write;

    /**
     * Counter for the number of readers.
     */
    std::atomic_uint n_readers;

public:
    ShmRwLock():n_readers(0) {
        lock_init(&write);
    }

    ~ShmRwLock() {
        lock_destroy(&write);
    }

    void ReadLock() {
        ++n_readers;

        if (!lock_is_locked(&write))
            /* no writer is waiting - we're done */
            return;

        /* slow route: undo the increment, and retry the increment while
           the write lock is held */

        --n_readers;

        lock_lock(&write);

        ++n_readers;

        lock_unlock(&write);
    }

    void ReadUnlock() {
        assert(n_readers > 0);

        --n_readers;
    }

    gcc_pure
    bool IsReadLocked() const {
        return n_readers > 0;
    }

    void WriteLock() {
        lock_lock(&write);

        /* wait for all readers to finish; new readers cannot appear,
           because write is locked */

        while (IsReadLocked())
            usleep(1);
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
