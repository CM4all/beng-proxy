/*
 * Functions for istream filters which just forward the input.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_FORWARD_H
#define __BENG_ISTREAM_FORWARD_H

#include "istream_oo.hxx"
#include "istream_pointer.hxx"
#include "istream-direct.h"
#include "glibfwd.hxx"

#include <stddef.h>
#include <sys/types.h>

class ForwardIstream : public Istream {
    IstreamPointer input;

protected:
    ForwardIstream(struct pool &pool, const struct istream_class &cls,
                   struct istream &_input,
                   const struct istream_handler &handler, void *ctx,
                   istream_direct_t direct=0)
        :Istream(pool, cls),
         input(_input, handler, ctx, direct) {}

    void CopyDirect() {
        input.SetDirect(GetHandlerDirect());
    }

public:
    /* istream */

    off_t GetAvailable(bool partial) {
        return input.GetAvailable(partial);
    }

    off_t Skip(off_t length) {
        return input.Skip(length);
    }

    void Read() {
        CopyDirect();
        input.Read();
    }

    int AsFd() {
        return input.AsFd();
    }

    void Close() {
        input.CloseHandler();
        Destroy();
    }

    /* handler */

    size_t OnData(const void *data, size_t length) {
        return InvokeData(data, length);
    }

    ssize_t OnDirect(enum istream_direct type, int fd, size_t max_length) {
        return InvokeDirect(type, fd, max_length);
    }

    void OnEof() {
        DestroyEof();
    }

    void OnError(GError *error) {
        DestroyError(error);
    }
};

size_t
istream_forward_data(const void *data, size_t length, void *ctx);

ssize_t
istream_forward_direct(enum istream_direct type, int fd, size_t max_length,
                       void *ctx);

void
istream_forward_eof(void *ctx);

void
istream_forward_abort(GError *error, void *ctx);

extern const struct istream_handler istream_forward_handler;

#endif
