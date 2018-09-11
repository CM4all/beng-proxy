/*
 * Copyright 2007-2018 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Crash handling.  The intention of this code is to determine if a
 * crash would require all workers to be restarted.
 */

#ifndef BENG_PROXY_CRASH_HXX
#define BENG_PROXY_CRASH_HXX

#include "util/Compiler.h"

#include <atomic>

#include <assert.h>

struct crash_shm {
    std::atomic_uint counter;

    crash_shm() noexcept:counter(0) {}
};

struct crash {
    struct crash_shm *shm;
};

extern struct crash global_crash;

/**
 * Throws on error.
 */
void
crash_init(struct crash *crash);

void
crash_deinit(struct crash *crash) noexcept;

/**
 * Throws on error.
 */
static inline void
crash_global_init()
{
    crash_init(&global_crash);
}

static inline void
crash_global_deinit() noexcept
{
    crash_deinit(&global_crash);
}

/**
 * Is the specified worker currently in a "safe" state?
 */
gcc_pure
static inline bool
crash_is_safe(struct crash *crash) noexcept
{
    assert(crash != nullptr);
    assert(crash->shm != nullptr);

    return crash->shm->counter == 0;
}

/**
 * Enter a code section that is possibly corrupting shared memory on a
 * crash.
 */
static inline void
crash_unsafe_enter(void) noexcept
{
    assert(global_crash.shm != nullptr);

    ++global_crash.shm->counter;
}

/**
 * Leave a code section that is possibly corrupting shared memory on a
 * crash.
 */
static inline void
crash_unsafe_leave() noexcept
{
    assert(global_crash.shm != nullptr);
    assert(!crash_is_safe(&global_crash));

    --global_crash.shm->counter;
}

/**
 * Is this process currently in "unsafe" state?
 */
gcc_pure
static inline bool
crash_in_unsafe() noexcept
{
    return !crash_is_safe(&global_crash);
}

struct ScopeCrashUnsafe {
    ScopeCrashUnsafe() noexcept {
        crash_unsafe_enter();
    }

    ~ScopeCrashUnsafe() noexcept {
        crash_unsafe_leave();
    }
};

#endif
