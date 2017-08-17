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
 * Wrappers for mmap functions.
 */

#ifndef BENG_PROXY_MMAP_H
#define BENG_PROXY_MMAP_H

#ifdef VALGRIND
#include <valgrind/memcheck.h>
#include <stdlib.h>
#endif

#include <stdbool.h>
#include <sys/mman.h>

gcc_const
static inline size_t
mmap_page_size(void)
{
#ifdef VALGRIND
    if (RUNNING_ON_VALGRIND)
        return 0x20;
#endif

    return 4096;
}

gcc_const
static inline size_t
mmap_huge_page_size(void)
{
#ifdef VALGRIND
    if (RUNNING_ON_VALGRIND)
        return 0x20;
#endif

#ifdef __linux
    return 512 * mmap_page_size();
#else
    return mmap_page_size();
#endif
}

static inline void *
mmap_alloc_anonymous(size_t size)
{
#ifdef VALGRIND
    if (RUNNING_ON_VALGRIND) {
        void *p = malloc(size);
        if (p == NULL)
            /* emulate mmap() error value */
            p = (void *)-1;
        return p;
    }
#endif

    int flags = MAP_ANONYMOUS|MAP_PRIVATE;

    return mmap(NULL, size, PROT_READ|PROT_WRITE, flags, -1, 0);
}

static inline void
mmap_free(void *p, size_t size)
{
#ifdef VALGRIND
    if (RUNNING_ON_VALGRIND) {
        free(p);
        return;
    }
#endif

    munmap(p, size);
}

/**
 * Allow the Linux kernel to use "Huge Pages" for the cache, which
 * reduces page table overhead for this big chunk of data.
 */
static inline void
mmap_enable_huge_pages(void *p, size_t size)
{
#ifdef VALGRIND
    if (RUNNING_ON_VALGRIND)
        return;
#endif

#if defined(__linux) && defined(MADV_HUGEPAGE)
    madvise(p, size, MADV_HUGEPAGE);
#else
    (void)p;
    (void)size;
#endif
}

/**
 * Controls whether forked processes inherit the specified pages.
 */
static inline void
mmap_enable_fork(void *p, size_t size, bool inherit)
{
#ifdef VALGRIND
    if (RUNNING_ON_VALGRIND)
        return;
#endif

#ifdef __linux
    madvise(p, size, inherit ? MADV_DOFORK : MADV_DONTFORK);
#else
    (void)p;
    (void)size;
    (void)inherit;
#endif
}

/**
 * Discard the specified page contents, giving memory back to the
 * kernel.  The mapping is preserved, and new memory will be allocated
 * automatically on the next write access.
 */
static inline void
mmap_discard_pages(void *p, size_t size)
{
#ifdef VALGRIND
    if (RUNNING_ON_VALGRIND) {
        VALGRIND_MAKE_MEM_UNDEFINED(p, size);
        return;
    }
#endif

#ifdef __linux
    madvise(p, size, MADV_DONTNEED);
#else
    (void)p;
    (void)size;
#endif
}

#endif
