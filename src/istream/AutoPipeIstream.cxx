/*
 * Copyright 2007-2018 Content Management AG
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

#include "AutoPipeIstream.hxx"
#include "PipeLease.hxx"
#include "New.hxx"
#include "UnusedPtr.hxx"
#include "ForwardIstream.hxx"
#include "direct.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "system/Error.hxx"
#include "io/Splice.hxx"

#include <assert.h>
#include <errno.h>
#include <string.h>

class AutoPipeIstream final : public ForwardIstream {
    PipeLease pipe;
    size_t piped = 0;

public:
    AutoPipeIstream(struct pool &p, UnusedIstreamPtr _input,
                    Stock *_pipe_stock) noexcept
        :ForwardIstream(p, std::move(_input)),
         pipe(_pipe_stock) {}

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) noexcept override;
    void _Read() noexcept override;

    void _FillBucketList(IstreamBucketList &list) override {
        if (piped > 0)
            return Istream::_FillBucketList(list);

        try {
            input.FillBucketList(list);
        } catch (...) {
            Destroy();
            throw;
        }
    }

    int _AsFd() noexcept override;
    void _Close() noexcept override;

    /* handler */
    size_t OnData(const void *data, size_t length) noexcept override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;

private:
    void CloseInternal() noexcept;
    void Abort(std::exception_ptr ep) noexcept;
    ssize_t Consume() noexcept;
};

void
AutoPipeIstream::CloseInternal() noexcept
{
    /* reuse the pipe only if it's empty */
    pipe.Release(piped == 0);
}

void
AutoPipeIstream::Abort(std::exception_ptr ep) noexcept
{
    CloseInternal();

    if (input.IsDefined())
        input.Close();

    DestroyError(ep);
}

ssize_t
AutoPipeIstream::Consume() noexcept
{
    assert(pipe.IsDefined());
    assert(piped > 0);

    ssize_t nbytes = InvokeDirect(FdType::FD_PIPE, pipe.GetReadFd().Get(),
                                  piped);
    if (gcc_unlikely(nbytes == ISTREAM_RESULT_BLOCKING ||
                     nbytes == ISTREAM_RESULT_CLOSED))
        /* handler blocks (-2) or pipe was closed (-3) */
        return nbytes;

    if (gcc_unlikely(nbytes == ISTREAM_RESULT_ERRNO && errno != EAGAIN)) {
        Abort(std::make_exception_ptr(MakeErrno("read from pipe failed")));
        return ISTREAM_RESULT_CLOSED;
    }

    if (nbytes > 0) {
        assert((size_t)nbytes <= piped);
        piped -= (size_t)nbytes;

        if (piped == 0)
            /* if the pipe was drained, return it to the stock, to
               make it available to other streams */
            pipe.ReleaseIfStock();

        if (piped == 0 && !input.IsDefined()) {
            /* our input has already reported EOF, and we have been
               waiting for the pipe buffer to become empty */
            CloseInternal();
            DestroyEof();
            return ISTREAM_RESULT_CLOSED;
        }
    }

    return nbytes;
}


/*
 * istream handler
 *
 */

inline size_t
AutoPipeIstream::OnData(const void *data, size_t length) noexcept
{
    assert(HasHandler());

    if (piped > 0) {
        ssize_t nbytes = Consume();
        if (nbytes == ISTREAM_RESULT_CLOSED)
            return 0;

        if (piped > 0 || !HasHandler())
            return 0;
    }

    assert(piped == 0);

    return InvokeData(data, length);
}

inline ssize_t
AutoPipeIstream::OnDirect(FdType type, int fd, size_t max_length) noexcept
{
    assert(HasHandler());
    assert(CheckDirect(FdType::FD_PIPE));

    if (piped > 0) {
        ssize_t nbytes = Consume();
        if (nbytes <= 0)
            return nbytes;

        if (piped > 0)
            /* if the pipe still isn't empty, we can't start reading
               new input */
            return ISTREAM_RESULT_BLOCKING;
    }

    if (CheckDirect(type))
        /* already supported by handler (maybe already a pipe) - no
           need for wrapping it into a pipe */
        return InvokeDirect(type, fd, max_length);

    assert((type & ISTREAM_TO_PIPE) == type);

    if (!pipe.IsDefined()) {
        try {
            pipe.Create(GetPool());
        } catch (...) {
            Abort(std::current_exception());
            return ISTREAM_RESULT_CLOSED;
        }
    }

    ssize_t nbytes = Splice(fd, pipe.GetWriteFd().Get(), max_length);
    /* don't check EAGAIN here (and don't return -2).  We assume that
       splicing to the pipe cannot possibly block, since we flushed
       the pipe; assume that it can only be the source file which is
       blocking */
    if (nbytes <= 0)
        return nbytes;

    assert(piped == 0);
    piped = (size_t)nbytes;

    if (Consume() == ISTREAM_RESULT_CLOSED)
        return ISTREAM_RESULT_CLOSED;

    return nbytes;
}

inline void
AutoPipeIstream::OnEof() noexcept
{
    input.Clear();

    pipe.CloseWriteIfNotStock();

    if (piped == 0) {
        CloseInternal();
        DestroyEof();
    }
}

inline void
AutoPipeIstream::OnError(std::exception_ptr ep) noexcept
{
    CloseInternal();
    input.Clear();
    DestroyError(ep);
}

/*
 * istream implementation
 *
 */

off_t
AutoPipeIstream::_GetAvailable(bool partial) noexcept
{
    if (gcc_likely(input.IsDefined())) {
        off_t available = input.GetAvailable(partial);
        if (piped > 0) {
            if (available != -1)
                available += piped;
            else if (partial)
                available = piped;
        }

        return available;
    } else {
        assert(piped > 0);

        return piped;
    }
}

void
AutoPipeIstream::_Read() noexcept
{
    if (piped > 0 && (Consume() <= 0 || piped > 0))
        return;

    /* at this point, the pipe must be flushed - if the pipe is
       flushed, this stream is either closed or there must be an input
       stream */
    assert(input.IsDefined());

    auto mask = GetHandlerDirect();
    if (mask & FdType::FD_PIPE)
        /* if the handler supports the pipe, we offer our services */
        mask |= ISTREAM_TO_PIPE;

    input.SetDirect(mask);
    input.Read();
}

int
AutoPipeIstream::_AsFd() noexcept
{
    if (piped > 0)
        /* need to flush the pipe buffer first */
        return -1;

    int fd = input.AsFd();
    if (fd >= 0) {
        CloseInternal();
        Destroy();
    }

    return fd;
}

void
AutoPipeIstream::_Close() noexcept
{
    CloseInternal();

    if (input.IsDefined())
        input.Close();

    Destroy();
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
NewAutoPipeIstream(struct pool *pool, UnusedIstreamPtr input,
                   Stock *pipe_stock) noexcept
{
    return NewIstreamPtr<AutoPipeIstream>(*pool, std::move(input), pipe_stock);
}
