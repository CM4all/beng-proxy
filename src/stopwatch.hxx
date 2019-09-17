/*
 * Copyright 2007-2019 Content Management AG
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

#pragma once

#include "util/Compiler.h"

#include <utility>

struct AllocatorPtr;
struct Stopwatch;
class SocketAddress;
class SocketDescriptor;

#ifdef ENABLE_STOPWATCH

class StopwatchPtr {
    Stopwatch *stopwatch = nullptr;

public:
    StopwatchPtr() = default;
    StopwatchPtr(std::nullptr_t) noexcept {}

    StopwatchPtr(AllocatorPtr alloc, const char *name,
                 const char *suffix=nullptr) noexcept;

    StopwatchPtr(AllocatorPtr alloc, SocketAddress address,
                 const char *suffix=nullptr) noexcept;

    StopwatchPtr(AllocatorPtr alloc, SocketDescriptor fd,
                 const char *suffix=nullptr) noexcept;

    StopwatchPtr(StopwatchPtr &&src) noexcept
        :stopwatch(std::exchange(src.stopwatch, nullptr)) {}

    ~StopwatchPtr() noexcept {
        if (stopwatch != nullptr)
            Destruct(*stopwatch);
    }

    operator bool() const noexcept {
        return stopwatch != nullptr;
    }

    void RecordEvent(const char *name) const noexcept;

    void Dump() const noexcept;

    void Finish(const char *name) const noexcept {
        RecordEvent(name);
        Dump();
    }

private:
    static void Destruct(Stopwatch &stopwatch) noexcept;
};

void
stopwatch_enable();

gcc_const
bool
stopwatch_is_enabled();

#else

#include "AllocatorPtr.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketDescriptor.hxx"

class StopwatchPtr {
public:
    StopwatchPtr() = default;
    StopwatchPtr(std::nullptr_t) noexcept {}

    template<typename N>
    StopwatchPtr(AllocatorPtr, N &&, const char *=nullptr) noexcept {}

    operator bool() const noexcept {
        return false;
    }

    void RecordEvent(const char *) const noexcept {}
    void Dump() const noexcept {}
    void Finish(const char *) const noexcept {}
};

static inline void
stopwatch_enable()
{
}

static inline bool
stopwatch_is_enabled()
{
    return false;
}

#endif
