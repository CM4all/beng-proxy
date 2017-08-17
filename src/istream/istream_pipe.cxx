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

#ifdef __linux

#include "istream_pipe.hxx"
#include "ForwardIstream.hxx"
#include "io/FileDescriptor.hxx"
#include "direct.hxx"
#include "pipe_stock.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "system/Error.hxx"
#include "io/Splice.hxx"

#include <assert.h>
#include <errno.h>
#include <string.h>

class PipeIstream final : public ForwardIstream {
    Stock *const stock;
    StockItem *stock_item = nullptr;
    FileDescriptor fds[2] = { FileDescriptor::Undefined(), FileDescriptor::Undefined() };
    size_t piped = 0;

public:
    PipeIstream(struct pool &p, Istream &_input,
                Stock *_pipe_stock)
        :ForwardIstream(p, _input),
         stock(_pipe_stock) {}

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override;
    void _Read() override;

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

    size_t _ConsumeBucketList(size_t nbytes) override {
        assert(piped == 0);

        auto consumed = input.ConsumeBucketList(nbytes);
        Consumed(consumed);
        return consumed;
    }

    int _AsFd() override;
    void _Close() override;

    /* handler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() override;
    void OnError(std::exception_ptr ep) override;

private:
    void CloseInternal();
    void Abort(std::exception_ptr ep);
    ssize_t Consume();

    /**
     * Throws exception on error.
     */
    void Create();
};

void
PipeIstream::CloseInternal()
{
    if (stock != nullptr) {
        if (stock_item != nullptr)
            /* reuse the pipe only if it's empty */
            stock_item->Put(piped > 0);
    } else {
        for (auto &fd : fds)
            if (fd.IsDefined())
                fd.Close();
    }
}

void
PipeIstream::Abort(std::exception_ptr ep)
{
    CloseInternal();

    if (input.IsDefined())
        input.Close();

    DestroyError(ep);
}

ssize_t
PipeIstream::Consume()
{
    assert(fds[0].IsDefined());
    assert(piped > 0);
    assert(stock_item != nullptr || stock == nullptr);

    ssize_t nbytes = InvokeDirect(FdType::FD_PIPE, fds[0].Get(), piped);
    if (unlikely(nbytes == ISTREAM_RESULT_BLOCKING ||
                 nbytes == ISTREAM_RESULT_CLOSED))
        /* handler blocks (-2) or pipe was closed (-3) */
        return nbytes;

    if (unlikely(nbytes == ISTREAM_RESULT_ERRNO && errno != EAGAIN)) {
        Abort(std::make_exception_ptr(MakeErrno("read from pipe failed")));
        return ISTREAM_RESULT_CLOSED;
    }

    if (nbytes > 0) {
        assert((size_t)nbytes <= piped);
        piped -= (size_t)nbytes;

        if (piped == 0 && stock != nullptr) {
            /* if the pipe was drained, return it to the stock, to
               make it available to other streams */

            stock_item->Put(false);
            stock_item = nullptr;

            for (auto &fd : fds)
                fd.SetUndefined();
        }

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
PipeIstream::OnData(const void *data, size_t length)
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

inline void
PipeIstream::Create()
{
    assert(!fds[0].IsDefined());
    assert(!fds[1].IsDefined());

    if (stock != nullptr) {
        assert(stock_item == nullptr);

        stock_item = stock->GetNow(GetPool(), nullptr);
        pipe_stock_item_get(stock_item, fds);
    } else {
        if (!FileDescriptor::CreatePipeNonBlock(fds[0], fds[1]))
            throw MakeErrno("pipe() failed");
    }
}

inline ssize_t
PipeIstream::OnDirect(FdType type, int fd, size_t max_length)
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

    if (!fds[1].IsDefined()) {
        try {
            Create();
        } catch (...) {
            Abort(std::current_exception());
            return ISTREAM_RESULT_CLOSED;
        }
    }

    ssize_t nbytes = Splice(fd, fds[1].Get(), max_length);
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
PipeIstream::OnEof()
{
    input.Clear();

    if (stock == nullptr && fds[1].IsDefined())
        fds[1].Close();

    if (piped == 0) {
        CloseInternal();
        DestroyEof();
    }
}

inline void
PipeIstream::OnError(std::exception_ptr ep)
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
PipeIstream::_GetAvailable(bool partial)
{
    if (likely(input.IsDefined())) {
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
PipeIstream::_Read()
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
PipeIstream::_AsFd()
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
PipeIstream::_Close()
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

Istream *
istream_pipe_new(struct pool *pool, Istream &input,
                 Stock *pipe_stock)
{
    return NewIstream<PipeIstream>(*pool, input, pipe_stock);
}

#endif
