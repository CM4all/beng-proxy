/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_SINK_HXX
#define BENG_PROXY_ISTREAM_SINK_HXX

#include "Pointer.hxx"

/**
 * An #IstreamHandler implementation which manages a pointer to its
 * #Istream instance.
 */
class IstreamSink : protected IstreamHandler {
protected:
    IstreamPointer input;

    IstreamSink()
        :input(nullptr) {}

    IstreamSink(Istream &_input, FdTypeMask direct=0)
        :input(_input, *this, direct) {}

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

    void ClearInput() {
        input.Clear();
    }

    void ClearAndCloseInput() {
        input.ClearAndClose();
    }
};

#endif
