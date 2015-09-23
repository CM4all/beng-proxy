/*
 * Convert any file descriptor to a pipe by splicing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifdef __linux

#include "istream_pipe.hxx"
#include "ForwardIstream.hxx"
#include "system/fd_util.h"
#include "direct.hxx"
#include "pipe_stock.hxx"
#include "stock/Stock.hxx"
#include "gerrno.h"

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

class PipeIstream final : public ForwardIstream {
    Stock *const stock;
    StockItem *stock_item = nullptr;
    int fds[2] = { -1, -1 };
    size_t piped = 0;

public:
    PipeIstream(struct pool &p, struct istream &_input,
                Stock *_pipe_stock);

    /* virtual methods from class Istream */

    off_t GetAvailable(bool partial) override;
    void Read() override;
    int AsFd() override;
    void Close() override;

    /* handler */
    size_t OnData(const void *data, size_t length);
    ssize_t OnDirect(FdType type, int fd, size_t max_length);
    void OnEof();
    void OnError(GError *error);

private:
    void CloseInternal();
    void Abort(GError *error);
    ssize_t Consume();
    bool Create();
};

void
PipeIstream::CloseInternal()
{
    if (stock != nullptr) {
        if (stock_item != nullptr)
            /* reuse the pipe only if it's empty */
            stock_put(*stock_item, piped > 0);
    } else {
        if (fds[0] >= 0) {
            close(fds[0]);
            fds[0] = -1;
        }

        if (fds[1] >= 0) {
            close(fds[1]);
            fds[1] = -1;
        }
    }
}

void
PipeIstream::Abort(GError *error)
{
    CloseInternal();

    if (input.IsDefined())
        input.Close();

    DestroyError(error);
}

ssize_t
PipeIstream::Consume()
{
    assert(fds[0] >= 0);
    assert(piped > 0);
    assert(stock_item != nullptr || stock == nullptr);

    ssize_t nbytes = InvokeDirect(FdType::FD_PIPE, fds[0], piped);
    if (unlikely(nbytes == ISTREAM_RESULT_BLOCKING ||
                 nbytes == ISTREAM_RESULT_CLOSED))
        /* handler blocks (-2) or pipe was closed (-3) */
        return nbytes;

    if (unlikely(nbytes == ISTREAM_RESULT_ERRNO && errno != EAGAIN)) {
        GError *error = new_error_errno_msg("read from pipe failed");
        Abort(error);
        return ISTREAM_RESULT_CLOSED;
    }

    if (nbytes > 0) {
        assert((size_t)nbytes <= piped);
        piped -= (size_t)nbytes;

        if (piped == 0 && stock != nullptr) {
            /* if the pipe was drained, return it to the stock, to
               make it available to other streams */

            stock_put(*stock_item, false);
            stock_item = nullptr;
            fds[0] = -1;
            fds[1] = -1;
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

inline bool
PipeIstream::Create()
{
    assert(fds[0] < 0);
    assert(fds[1] < 0);

    if (stock != nullptr) {
        assert(stock_item == nullptr);

        GError *error = nullptr;
        stock_item = stock_get_now(*stock, GetPool(), nullptr, &error);
        if (stock_item == nullptr) {
            daemon_log(1, "%s\n", error->message);
            g_error_free(error);
            return false;
        }

        pipe_stock_item_get(stock_item, fds);
    } else {
        if (pipe_cloexec_nonblock(fds) < 0) {
            daemon_log(1, "pipe() failed: %s\n", strerror(errno));
            return false;
        }
    }

    return true;
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

    if (fds[1] < 0 && !Create())
        return ISTREAM_RESULT_CLOSED;

    ssize_t nbytes = splice(fd, nullptr, fds[1], nullptr, max_length,
                            SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
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

    if (stock == nullptr && fds[1] >= 0) {
        close(fds[1]);
        fds[1] = -1;
    }

    if (piped == 0) {
        CloseInternal();
        DestroyEof();
    }
}

inline void
PipeIstream::OnError(GError *error)
{
    CloseInternal();
    input.Clear();
    DestroyError(error);
}

/*
 * istream implementation
 *
 */

off_t
PipeIstream::GetAvailable(bool partial)
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
PipeIstream::Read()
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
PipeIstream::AsFd()
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
PipeIstream::Close()
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

PipeIstream::PipeIstream(struct pool &p, struct istream &_input,
                         Stock *_pipe_stock)
    :ForwardIstream(p, _input, MakeIstreamHandler<PipeIstream>::handler, this),
     stock(_pipe_stock)
{
}

struct istream *
istream_pipe_new(struct pool *pool, struct istream *input,
                 Stock *pipe_stock)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    return NewIstream<PipeIstream>(*pool, *input, pipe_stock);
}

#endif
