/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
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
