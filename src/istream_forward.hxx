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
    ForwardIstream(struct pool &pool, struct istream &_input,
                   const struct istream_handler &handler, void *ctx,
                   FdTypeMask direct=0)
        :FacadeIstream(pool, _input, handler, ctx, direct) {}

    explicit ForwardIstream(struct pool &pool)
        :FacadeIstream(pool) {}

public:
    /* virtual methods from class Istream */

    off_t GetAvailable(bool partial) override {
        return input.GetAvailable(partial);
    }

    off_t Skip(off_t length) override {
        return input.Skip(length);
    }

    void Read() override {
        CopyDirect();
        input.Read();
    }

    int AsFd() override {
        int fd = input.AsFd();
        if (fd >= 0)
            Destroy();
        return fd;
    }

    void Close() override {
        input.CloseHandler();
        Istream::Close();
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
