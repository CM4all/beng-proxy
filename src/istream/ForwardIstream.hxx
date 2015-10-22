/*
 * Functions for istream filters which just forward the input.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_FORWARD_H
#define __BENG_ISTREAM_FORWARD_H

#include "FacadeIstream.hxx"
#include "glibfwd.hxx"

#include <stddef.h>
#include <sys/types.h>

class ForwardIstream : public FacadeIstream {
protected:
    ForwardIstream(struct pool &_pool, struct istream &_input,
                   const struct istream_handler &_handler, void *ctx,
                   FdTypeMask direct=0)
        :FacadeIstream(_pool, _input, _handler, ctx, direct) {}

    explicit ForwardIstream(struct pool &_pool)
        :FacadeIstream(_pool) {}

public:
    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override {
        return input.GetAvailable(partial);
    }

    off_t _Skip(off_t length) override {
        return input.Skip(length);
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

    /* handler */

    size_t OnData(const void *data, size_t length) {
        return InvokeData(data, length);
    }

    ssize_t OnDirect(FdType type, int fd, size_t max_length) {
        return InvokeDirect(type, fd, max_length);
    }

    void OnEof() {
        DestroyEof();
    }

    void OnError(GError *error) {
        DestroyError(error);
    }
};

#endif
