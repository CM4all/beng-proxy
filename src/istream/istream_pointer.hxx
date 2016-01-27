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

    void ClearHandler() {
        assert(IsDefined());

        stream->ClearHandler();
        Clear();
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

    bool FillBucketList(IstreamBucketList &list, GError **error_r) {
        assert(IsDefined());

        return stream->FillBucketList(list, error_r);
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
