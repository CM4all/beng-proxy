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

#ifndef BENG_PROXY_STOPWATCH_HXX
#define BENG_PROXY_STOPWATCH_HXX

#include "util/Compiler.h"

#include <stddef.h>

struct pool;
struct Stopwatch;
class SocketAddress;
class SocketDescriptor;

#ifdef ENABLE_STOPWATCH

void
stopwatch_enable();

gcc_const
bool
stopwatch_is_enabled();

Stopwatch *
stopwatch_new(struct pool *pool, const char *name, const char *suffix=nullptr);

Stopwatch *
stopwatch_new(struct pool *pool, SocketAddress address, const char *suffix);

Stopwatch *
stopwatch_new(struct pool *pool, SocketDescriptor fd, const char *suffix);

void
stopwatch_event(Stopwatch *stopwatch, const char *name);

void
stopwatch_dump(const Stopwatch *stopwatch);

#else

#include "net/SocketAddress.hxx"
#include "net/SocketDescriptor.hxx"

static inline void
stopwatch_enable()
{
}

static inline bool
stopwatch_is_enabled()
{
    return false;
}

static inline Stopwatch *
stopwatch_new(struct pool *pool, const char *name, const char *suffix=nullptr)
{
    (void)pool;
    (void)name;
    (void)suffix;

    return nullptr;
}

static inline Stopwatch *
stopwatch_new(struct pool *pool, SocketAddress, const char *suffix)
{
    (void)pool;
    (void)suffix;

    return nullptr;
}

static inline Stopwatch *
stopwatch_new(struct pool *pool, SocketDescriptor, const char *suffix)
{
    (void)pool;
    (void)suffix;

    return nullptr;
}

static inline void
stopwatch_event(Stopwatch *stopwatch, const char *name)
{
    (void)stopwatch;
    (void)name;
}

static inline void
stopwatch_dump(const Stopwatch *stopwatch)
{
    (void)stopwatch;
}

#endif

#endif
