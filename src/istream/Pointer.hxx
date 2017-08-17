/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk.com>
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

#include <cstddef>
#include <cassert>

class IstreamPointer {
    Istream *stream;

public:
    IstreamPointer() = default;
    explicit IstreamPointer(std::nullptr_t):stream(nullptr) {}

    IstreamPointer(Istream &_stream,
                   IstreamHandler &handler,
                   FdTypeMask direct=0)
        :stream(&_stream) {
        stream->SetHandler(handler, direct);
    }

    explicit IstreamPointer(Istream *_stream,
                            IstreamHandler &handler,
                            FdTypeMask direct=0)
        :stream(_stream) {
        if (stream != nullptr)
            stream->SetHandler(handler, direct);
    }

    IstreamPointer(IstreamPointer &&other)
        :stream(other.stream) {
        other.stream = nullptr;
    }

    IstreamPointer(const IstreamPointer &) = delete;
    IstreamPointer &operator=(const IstreamPointer &) = delete;

    bool IsDefined() const {
        return stream != nullptr;
    }

    void Clear() {
        stream = nullptr;
    }

    void Close() {
        assert(IsDefined());

        stream->Close();
    }

    void ClearAndClose() {
        assert(IsDefined());

        auto *old = stream;
        Clear();
        old->Close();
    }

    Istream *Steal() {
        Istream *result = stream;
        stream = nullptr;
        return result;
    }

    void Set(Istream &_stream,
             IstreamHandler &handler,
             FdTypeMask direct=0) {
        assert(!IsDefined());

        stream = &_stream;
        stream->SetHandler(handler, direct);
    }

    void Replace(Istream &_stream,
                 IstreamHandler &handler,
                 FdTypeMask direct=0) {
        Close();

        stream = &_stream;
        stream->SetHandler(handler, direct);
    }

    void SetDirect(FdTypeMask direct) {
        assert(IsDefined());

        stream->SetDirect(direct);
    }

    void SetDirect(const Istream &src) {
        SetDirect(src.GetHandlerDirect());
    }

    void Read() {
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
    off_t GetAvailable(bool partial) const {
        assert(IsDefined());

        return stream->GetAvailable(partial);
    }

    off_t Skip(off_t length) {
        assert(IsDefined());

        return stream->Skip(length);
    }

    int AsFd() {
        assert(IsDefined());

        return stream->AsFd();
    }
};

#endif
