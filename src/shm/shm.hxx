/*
 * Copyright 2007-2017 Content Management AG
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
 * Shared memory for sharing data between worker processes.
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
