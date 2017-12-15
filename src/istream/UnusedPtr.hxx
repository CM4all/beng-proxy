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

#ifndef BENG_PROXY_UNUSED_ISTREAM_PTR_HXX
#define BENG_PROXY_UNUSED_ISTREAM_PTR_HXX

#include "util/Compiler.h"

#include <utility>

#include <sys/types.h>

class Istream;

/**
 * This class holds a pointer to an unused #Istream and auto-closes
 * it.  It can be moved to other instances, until it is finally
 * "stolen" using Steal() to actually use it.
 */
class UnusedIstreamPtr {
    Istream *stream = nullptr;

public:
    UnusedIstreamPtr() = default;
    UnusedIstreamPtr(std::nullptr_t) noexcept {}

    explicit UnusedIstreamPtr(Istream *_stream)
        :stream(_stream) {}

    UnusedIstreamPtr(UnusedIstreamPtr &&src)
        :stream(std::exchange(src.stream, nullptr)) {}

    ~UnusedIstreamPtr() {
        if (stream != nullptr)
            Close(*stream);
    }

    UnusedIstreamPtr &operator=(UnusedIstreamPtr &&src) {
        using std::swap;
        swap(stream, src.stream);
        return *this;
    }

    operator bool() const {
        return stream != nullptr;
    }

    Istream *Steal() {
        return std::exchange(stream, nullptr);
    }

    void Clear() {
        auto *s = Steal();
        if (s != nullptr)
            Close(*s);
    }

    gcc_pure
    off_t GetAvailable(bool partial) const noexcept;

    /**
     * Calls Istream::AsFd().  On successful (non-negative) return
     * value, this object is cleared.
     */
    int AsFd() noexcept;

private:
    static void Close(Istream &i);
};

#endif
