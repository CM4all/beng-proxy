/*
 * Functions for istream filters which just forward the input.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_FACADE_HXX
#define BENG_PROXY_ISTREAM_FACADE_HXX

#include "istream_oo.hxx"
#include "istream_pointer.hxx"

class FacadeIstream : public Istream {
protected:
    IstreamPointer input;

    FacadeIstream(struct pool &_pool, struct istream &_input,
                  const struct istream_handler &_handler, void *ctx,
                  FdTypeMask direct=0)
        :Istream(_pool),
         input(_input, _handler, ctx, direct) {}

    explicit FacadeIstream(struct pool &_pool)
        :Istream(_pool), input(nullptr) {}

    void CopyDirect() {
        input.SetDirect(GetHandlerDirect());
    }

    bool HasInput() const {
        return input.IsDefined();
    }

    void SetInput(struct istream &_input,
                  const struct istream_handler &_handler, void *ctx,
                  FdTypeMask direct=0) {
        input.Set(_input, _handler, ctx, direct);
    }

    void ReplaceInput(struct istream &_input,
                      const struct istream_handler &_handler, void *ctx,
                      FdTypeMask direct=0) {
        input.Replace(_input, _handler, ctx, direct);
    }

    void ReplaceInputDirect(struct istream &_input,
                            const struct istream_handler &_handler, void *ctx) {
        assert(input.IsDefined());

        input.Replace(_input, _handler, ctx, GetHandlerDirect());
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
