/*
 * Fault injection istream filter.  This istream forwards data from
 * its input, but will never forward eof/abort.  The "abort" can be
 * injected at any time.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_inject.hxx"
#include "istream_internal.hxx"
#include "istream_forward.hxx"
#include "util/Cast.hxx"

#include <assert.h>

class InjectIstream final : public ForwardIstream {
public:
    InjectIstream(struct pool &p, struct istream &_input)
        :ForwardIstream(p, _input,
                        MakeIstreamHandler<InjectIstream>::handler, this) {}

    static constexpr InjectIstream &Cast2(struct istream &i) {
        return (InjectIstream &)Istream::Cast(i);
    }

    void InjectFault(GError *error) {
        if (HasInput())
            input.Close();

        DestroyError(error);
    }

    /* virtual methods from class Istream */

    off_t GetAvailable(bool partial) override {
        /* never return the total length, because the caller may then
           make assumptions on when this stream ends */
        return partial && HasInput()
            ? ForwardIstream::GetAvailable(partial)
            : -1;
    }

    void Read() override {
        if (HasInput())
            ForwardIstream::Read();
    }

    int AsFd() override {
        return -1;
    }

    /* handler */

    void OnEof() {
        ClearInput();
    }

    void OnError(GError *error) {
        g_error_free(error);
        ClearInput();
    }
};

/*
 * constructor
 *
 */

struct istream *
istream_inject_new(struct pool *pool, struct istream *input)
{
    return NewIstream<InjectIstream>(*pool, *input);
}

void
istream_inject_fault(struct istream *i_inject, GError *error)
{
    auto &inject = InjectIstream::Cast2(*i_inject);
    inject.InjectFault(error);
}
