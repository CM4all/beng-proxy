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

#ifndef BENG_PROXY_ISTREAM_POINTER_HXX
#define BENG_PROXY_ISTREAM_POINTER_HXX

#include "istream.hxx"

#include <utility>
#include <cstddef>
#include <cassert>

class UnusedIstreamPtr;

class IstreamPointer {
    Istream *stream;

public:
    IstreamPointer() = default;
    explicit IstreamPointer(std::nullptr_t) noexcept:stream(nullptr) {}

    IstreamPointer(UnusedIstreamPtr src,
                   IstreamHandler &handler,
                   FdTypeMask direct=0) noexcept;

    IstreamPointer(IstreamPointer &&other) noexcept
        :stream(std::exchange(other.stream, nullptr)) {}

    IstreamPointer(const IstreamPointer &) = delete;
    IstreamPointer &operator=(const IstreamPointer &) = delete;

    bool IsDefined() const noexcept {
        return stream != nullptr;
    }

    void Clear() noexcept {
        stream = nullptr;
    }

    void Close() noexcept {
        assert(IsDefined());

        stream->Close();
    }

    void ClearAndClose() noexcept {
        assert(IsDefined());

        auto *old = stream;
        Clear();
        old->Close();
    }

    Istream *Steal() noexcept {
        Istream *result = stream;
        stream = nullptr;
        return result;
    }

    void Set(UnusedIstreamPtr _stream,
             IstreamHandler &handler,
             FdTypeMask direct=0) noexcept;

    void Set(Istream &_stream,
             IstreamHandler &handler,
             FdTypeMask direct=0) noexcept {
        assert(!IsDefined());

        stream = &_stream;
        stream->SetHandler(handler, direct);
    }

    template<typename I>
    void Replace(I &&_stream,
                 IstreamHandler &handler,
                 FdTypeMask direct=0) noexcept {
        Close();

#ifndef NDEBUG
        /* must clear the pointer in the debug build to avoid
           assertion failure in Set() */
        Clear();
#endif

        Set(std::forward<I>(_stream), handler, direct);
    }

    void SetDirect(FdTypeMask direct) noexcept {
        assert(IsDefined());

        stream->SetDirect(direct);
    }

    void SetDirect(const Istream &src) noexcept {
        SetDirect(src.GetHandlerDirect());
    }

    void Read() noexcept {
        assert(IsDefined());

        stream->Read();
    }

    void FillBucketList(IstreamBucketList &list) {
        assert(IsDefined());

        stream->FillBucketList(list);
    }

    size_t ConsumeBucketList(size_t nbytes) {
        assert(IsDefined());

        return stream->ConsumeBucketList(nbytes);
    }

    gcc_pure
    off_t GetAvailable(bool partial) const noexcept {
        assert(IsDefined());

        return stream->GetAvailable(partial);
    }

    off_t Skip(off_t length) noexcept {
        assert(IsDefined());

        return stream->Skip(length);
    }

    int AsFd() {
        assert(IsDefined());

        return stream->AsFd();
    }
};

#endif
