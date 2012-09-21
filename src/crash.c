/*
 * Crash handling.  The intention of this code is to determine if a
 * crash would require all workers to be restarted.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "crash.h"

#include <sys/mman.h>

struct crash global_crash;

bool
crash_init(struct crash *crash)
{
    assert(crash != NULL);

    crash->shm = mmap(NULL, sizeof(*crash->shm),
                      PROT_READ|PROT_WRITE,
                      MAP_ANONYMOUS|MAP_SHARED,
                      -1, 0);
    if (crash->shm == (struct crash_shm *)-1)
        return false;

    g_atomic_int_set(&crash->shm->counter, 0);
    return true;
}

void
crash_deinit(struct crash *crash)
{
    assert(crash != NULL);
    assert(crash->shm != NULL);

    munmap(crash->shm, sizeof(*crash->shm));
}
