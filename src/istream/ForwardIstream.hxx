/*
 * Functions for istream filters which just forward the input.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_FORWARD_H
#define __BENG_ISTREAM_FORWARD_H

#include "FacadeIstream.hxx"

#include <stddef.h>
#include <sys/types.h>

class ForwardIstream : public FacadeIstream {
protected:
    ForwardIstream(struct pool &_pool, Istream &_input,
                   FdTypeMask direct=0)
        :FacadeIstream(_pool, _input, direct) {}

    explicit ForwardIstream(struct pool &_pool)
        :FacadeIstream(_pool) {}

public:
    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override {
        return input.GetAvailable(partial);
    }

    off_t _Skip(off_t length) override {
        off_t nbytes = input.Skip(length);
        if (nbytes > 0)
            Consumed(nbytes);
        return nbytes;
    }

    void _Read() override {
        CopyDirect();
        input.Read();
    }

    int _AsFd() override {
        int fd = input.AsFd();
        if (fd >= 0)
            Destroy();
        return fd;
    }

    void _Close() override {
        input.Close();
        Istream::_Close();
    }

    /* virtual methods from class IstreamHandler */

    size_t OnData(const void *data, size_t length) override {
        return InvokeData(data, length);
    }

    ssize_t OnDirect(FdType type, int fd, size_t max_length) override {
        return InvokeDirect(type, fd, max_length);
    }

    void OnEof() override {
        DestroyEof();
    }

    void OnError(std::exception_ptr ep) override {
        DestroyError(ep);
    }
};

#endif
