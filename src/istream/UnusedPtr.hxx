/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_UNUSED_ISTREAM_PTR_HXX
#define BENG_PROXY_UNUSED_ISTREAM_PTR_HXX

#include "istream.hxx"

#include <utility>

/**
 * This class holds a pointer to an unused #Istream and auto-closes
 * it.  It can be moved to other instances, until it is finally
 * "stolen" using Steal() to actually use it.
 */
class UnusedIstreamPtr {
    Istream *stream = nullptr;

public:
    UnusedIstreamPtr() = default;

    explicit UnusedIstreamPtr(Istream *_stream)
        :stream(_stream) {}

    UnusedIstreamPtr(UnusedIstreamPtr &&src):stream(src.stream) {
        src.stream = nullptr;
    }

    ~UnusedIstreamPtr() {
        if (stream != nullptr)
            stream->CloseUnused();
    }

    UnusedIstreamPtr &operator=(UnusedIstreamPtr &&src) {
        using std::swap;
        swap(stream, src.stream);
        return *this;
    }

    Istream *Steal() {
        return std::exchange(stream, nullptr);
    }

    void Clear() {
        auto *s = Steal();
        if (s != nullptr)
            s->Close();
    }
};

#endif
