/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_POINTER_HXX
#define BENG_PROXY_ISTREAM_POINTER_HXX

#include "istream.h"

#include <cstddef>
#include <cassert>

class IstreamPointer {
    struct istream *stream;

public:
    IstreamPointer() = default;
    explicit IstreamPointer(std::nullptr_t):stream(nullptr) {}

    explicit IstreamPointer(struct istream &_stream,
                            const struct istream_handler &handler, void *ctx,
                            istream_direct_t direct=0)
        :stream(&_stream) {
        istream_handler_set(stream, &handler, ctx, direct);
    }

    explicit IstreamPointer(struct istream *_stream,
                            const struct istream_handler &handler, void *ctx,
                            istream_direct_t direct=0)
        :stream(_stream) {
        if (stream != nullptr)
            istream_handler_set(stream, &handler, ctx, direct);
    }

    bool IsDefined() const {
        return stream != nullptr;
    }

    void Clear() {
        stream = nullptr;
    }

    void ClearAndClose() {
        assert(IsDefined());

        istream_free(&stream);
    }

    void ClearHandler() {
        assert(IsDefined());

        istream_handler_clear(stream);
        Clear();
    }

    void ClearHandlerAndClose() {
        assert(IsDefined());

        istream_free_handler(&stream);
    }

    void CloseHandler() {
        assert(IsDefined());

        istream_close_handler(stream);
    }

    void SetDirect(istream_direct_t direct) {
        istream_handler_set_direct(stream, direct);
    }

    void SetDirect(const struct istream &src) {
        SetDirect(src.handler_direct);
    }

    void Read() {
        istream_read(stream);
    }

    gcc_pure
    off_t GetAvailable(bool partial) const {
        return istream_available(stream, partial);
    }
};

#endif
