/*
 * Crash handling.  The intention of this code is to determine if a
 * crash would require all workers to be restarted.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "crash.hxx"

#include <new>

#include <sys/mman.h>

struct crash global_crash;

bool
crash_init(struct crash *crash)
{
    assert(crash != nullptr);

    void *p = mmap(nullptr, sizeof(*crash->shm),
             PROT_READ|PROT_WRITE,
             MAP_ANONYMOUS|MAP_SHARED,
             -1, 0);
    if (p == (struct crash_shm *)-1)
        return false;

    crash->shm = new(p) crash_shm();
    return true;
}

void
crash_deinit(struct crash *crash)
{
    assert(crash != nullptr);
    assert(crash->shm != nullptr);

    munmap(crash->shm, sizeof(*crash->shm));
}
