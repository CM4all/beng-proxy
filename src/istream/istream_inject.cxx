/*
 * Fault injection istream filter.  This istream forwards data from
 * its input, but will never forward eof/abort.  The "abort" can be
 * injected at any time.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_inject.hxx"
#include "ForwardIstream.hxx"

#include <glib.h>

class InjectIstream final : public ForwardIstream {
public:
    InjectIstream(struct pool &p, Istream &_input)
        :ForwardIstream(p, _input) {}

    void InjectFault(GError *error) {
        if (HasInput())
            input.Close();

        DestroyError(error);
    }

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override {
        /* never return the total length, because the caller may then
           make assumptions on when this stream ends */
        return partial && HasInput()
            ? ForwardIstream::_GetAvailable(partial)
            : -1;
    }

    void _Read() override {
        if (HasInput())
            ForwardIstream::_Read();
    }

    int _AsFd() override {
        return -1;
    }

    /* virtual methods from class IstreamHandler */

    void OnEof() override {
        ClearInput();
    }

    void OnError(GError *error) override {
        g_error_free(error);
        ClearInput();
    }
};

/*
 * constructor
 *
 */

Istream *
istream_inject_new(struct pool &pool, Istream &input)
{
    return NewIstream<InjectIstream>(pool, input);
}

void
istream_inject_fault(Istream &i_inject, GError *error)
{
    auto &inject = (InjectIstream &)i_inject;
    inject.InjectFault(error);
}
