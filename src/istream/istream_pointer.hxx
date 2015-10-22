/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_POINTER_HXX
#define BENG_PROXY_ISTREAM_POINTER_HXX

#include "istream_oo.hxx"

#include <cstddef>
#include <cassert>

class IstreamPointer {
    Istream *stream;

public:
    IstreamPointer() = default;
    explicit IstreamPointer(std::nullptr_t):stream(nullptr) {}

    IstreamPointer(struct Istream &_stream,
                   const struct istream_handler &handler, void *ctx,
                   FdTypeMask direct=0)
        :stream(&_stream) {
        stream->SetHandler(handler, ctx, direct);
    }

    IstreamPointer(struct istream &_stream,
                   const struct istream_handler &handler, void *ctx,
                   FdTypeMask direct=0)
        :IstreamPointer(Istream::Cast(_stream), handler, ctx, direct) {}

    explicit IstreamPointer(Istream *_stream,
                            const struct istream_handler &handler, void *ctx,
                            FdTypeMask direct=0)
        :stream(_stream) {
        if (stream != nullptr)
            stream->SetHandler(handler, ctx, direct);
    }

    IstreamPointer(struct istream *_stream,
                   const struct istream_handler &handler, void *ctx,
                   FdTypeMask direct=0)
        :IstreamPointer(Istream::Cast(_stream), handler, ctx, direct) {}

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

    void ClearHandler() {
        assert(IsDefined());

        stream->ClearHandler();
        Clear();
    }

    void Set(Istream &_stream,
             const struct istream_handler &handler, void *ctx,
             FdTypeMask direct=0) {
        assert(!IsDefined());

        stream = &_stream;
        stream->SetHandler(handler, ctx, direct);
    }

    void Set(struct istream &_stream,
             const struct istream_handler &handler, void *ctx,
             FdTypeMask direct=0) {
        Set(Istream::Cast(_stream), handler, ctx, direct);
    }

    void Replace(Istream &_stream,
                 const struct istream_handler &handler, void *ctx,
                 FdTypeMask direct=0) {
        Close();

        stream = &_stream;
        stream->SetHandler(handler, ctx, direct);
    }

    void Replace(struct istream &_stream,
                 const struct istream_handler &handler, void *ctx,
                 FdTypeMask direct=0) {
        Replace(Istream::Cast(_stream), handler, ctx, direct);
    }

    void SetDirect(FdTypeMask direct) {
        assert(IsDefined());

        stream->SetDirect(direct);
    }

    void SetDirect(const struct istream &src) {
        SetDirect(src.handler_direct);
    }

    void Read() {
        assert(IsDefined());

        stream->Read();
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
