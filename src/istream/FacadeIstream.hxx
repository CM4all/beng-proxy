/*
 * Functions for istream filters which just forward the input.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_FACADE_HXX
#define BENG_PROXY_ISTREAM_FACADE_HXX

#include "istream_oo.hxx"
#include "Pointer.hxx"

class FacadeIstream : public Istream, protected IstreamHandler {
protected:
    IstreamPointer input;

    FacadeIstream(struct pool &_pool, Istream &_input,
                  FdTypeMask direct=0)
        :Istream(_pool),
         input(_input, *this, direct) {}

    explicit FacadeIstream(struct pool &_pool)
        :Istream(_pool), input(nullptr) {}

    void CopyDirect() {
        input.SetDirect(GetHandlerDirect());
    }

    bool HasInput() const {
        return input.IsDefined();
    }

    void SetInput(Istream &_input,
                  FdTypeMask direct=0) {
        input.Set(_input, *this, direct);
    }

    void ReplaceInput(Istream &_input,
                      FdTypeMask direct=0) {
        input.Replace(_input, *this, direct);
    }

    void ReplaceInputDirect(Istream &_input) {
        assert(input.IsDefined());

        input.Replace(_input, *this, GetHandlerDirect());
    }

    void ClearInput() {
        input.Clear();
    }

    void ClearInputHandler() {
        input.ClearHandler();
    }

    void ClearAndCloseInput() {
        input.ClearAndClose();
    }
};

#endif
