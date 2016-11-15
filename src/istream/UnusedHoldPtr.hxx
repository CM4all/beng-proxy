/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_UNUSED_HOLD_ISTREAM_PTR_HXX
#define BENG_PROXY_UNUSED_HOLD_ISTREAM_PTR_HXX

#include "UnusedPtr.hxx"
#include "istream_hold.hxx"

/**
 * A variant of #UnusedIstreamPtr which wraps the #Istream with
 * istream_hold_new(), to make it safe to be used in asynchronous
 * context.
 */
class UnusedHoldIstreamPtr : public UnusedIstreamPtr {
public:
    UnusedHoldIstreamPtr() = default;

    explicit UnusedHoldIstreamPtr(struct pool &p, Istream *_stream)
        :UnusedIstreamPtr(_stream != nullptr
                          ? istream_hold_new(p, *_stream)
                          : nullptr) {}

    UnusedHoldIstreamPtr(UnusedHoldIstreamPtr &&src) = default;

    UnusedHoldIstreamPtr &operator=(UnusedHoldIstreamPtr &&src) = default;

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
