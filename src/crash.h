/*
 * Crash handling.  The intention of this code is to determine if a
 * crash would require all workers to be restarted.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CRASH_H
#define BENG_PROXY_CRASH_H

#include <inline/compiler.h>

#include <glib.h>
#include <assert.h>
#include <stdbool.h>

struct crash_shm {
    volatile gint counter;
};

struct crash {
    struct crash_shm *shm;
};

extern struct crash global_crash;

#ifdef __cplusplus
extern "C" {
#endif

bool
crash_init(struct crash *crash);

void
crash_deinit(struct crash *crash);

static inline bool
crash_global_init(void)
{
    return crash_init(&global_crash);
}

static inline void
crash_global_deinit(void)
{
    crash_deinit(&global_crash);
}

/**
 * Is the specified worker currently in a "safe" state?
 */
gcc_pure
static inline bool
crash_is_safe(struct crash *crash)
{
    assert(crash != NULL);
    assert(crash->shm != NULL);

    return g_atomic_int_get(&crash->shm->counter) == 0;
}

/**
 * Enter a code section that is possibly corrupting shared memory on a
 * crash.
 */
static inline void
crash_unsafe_enter(void)
{
    assert(global_crash.shm != NULL);

    g_atomic_int_inc(&global_crash.shm->counter);
}

/**
 * Leave a code section that is possibly corrupting shared memory on a
 * crash.
 */
static inline void
crash_unsafe_leave(void)
{
    assert(global_crash.shm != NULL);
    assert(!crash_is_safe(&global_crash));

    (void)g_atomic_int_dec_and_test(&global_crash.shm->counter);
}

/**
 * Is this process currently in "unsafe" state?
 */
gcc_pure
static inline bool
crash_in_unsafe(void)
{
    return !crash_is_safe(&global_crash);
}

#ifdef __cplusplus
}
#endif

#endif
