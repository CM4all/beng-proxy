/*
 * Functions for istream filters which just forward the input.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_FACADE_HXX
#define BENG_PROXY_ISTREAM_FACADE_HXX

#include "istream.hxx"
#include "Sink.hxx"

class FacadeIstream : public Istream, protected IstreamSink {
protected:
    FacadeIstream(struct pool &_pool, Istream &_input,
                  FdTypeMask direct=0)
        :Istream(_pool), IstreamSink(_input, direct) {}

    explicit FacadeIstream(struct pool &_pool)
        :Istream(_pool) {}

    void CopyDirect() {
        input.SetDirect(GetHandlerDirect());
    }

    void ReplaceInputDirect(Istream &_input) {
        assert(input.IsDefined());

        input.Replace(_input, *this, GetHandlerDirect());
    }
};

#endif
