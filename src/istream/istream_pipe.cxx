/*
 * Convert any file descriptor to a pipe by splicing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifdef __linux

#include "istream_pipe.hxx"
#include "istream_pointer.hxx"
#include "istream_internal.hxx"
#include "fd_util.h"
#include "direct.hxx"
#include "pipe_stock.hxx"
#include "stock.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

struct PipeIstream {
    struct istream output;
    IstreamPointer input;
    Stock *const stock;
    StockItem *stock_item = nullptr;
    int fds[2] = { -1, -1 };
    size_t piped = 0;

    PipeIstream(struct pool &p, struct istream &_input,
                Stock *_pipe_stock);

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

    istream_deinit_abort(&output, error);
}

ssize_t
PipeIstream::Consume()
{
    assert(fds[0] >= 0);
    assert(piped > 0);
    assert(stock_item != nullptr || stock == nullptr);

    ssize_t nbytes = istream_invoke_direct(&output, FdType::FD_PIPE,
                                           fds[0], piped);
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
            istream_deinit_eof(&output);
            return ISTREAM_RESULT_CLOSED;
        }
    }

    return nbytes;
}


/*
 * istream handler
 *
 */

static size_t
pipe_input_data(const void *data, size_t length, void *ctx)
{
    PipeIstream *p = (PipeIstream *)ctx;

    assert(p->output.handler != nullptr);

    if (p->piped > 0) {
        ssize_t nbytes = p->Consume();
        if (nbytes == ISTREAM_RESULT_CLOSED)
            return 0;

        if (p->piped > 0 || p->output.handler == nullptr)
            return 0;
    }

    assert(p->piped == 0);

    return istream_invoke_data(&p->output, data, length);
}

inline bool
PipeIstream::Create()
{
    assert(fds[0] < 0);
    assert(fds[1] < 0);

    if (stock != nullptr) {
        assert(stock_item == nullptr);

        GError *error = nullptr;
        stock_item = stock_get_now(*stock, *output.pool, nullptr, &error);
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

static ssize_t
pipe_input_direct(FdType type, int fd, size_t max_length, void *ctx)
{
    PipeIstream *p = (PipeIstream *)ctx;

    assert(p->output.handler != nullptr);
    assert(p->output.handler->direct != nullptr);
    assert(istream_check_direct(&p->output, FdType::FD_PIPE));

    if (p->piped > 0) {
        ssize_t nbytes = p->Consume();
        if (nbytes <= 0)
            return nbytes;

        if (p->piped > 0)
            /* if the pipe still isn't empty, we can't start reading
               new input */
            return ISTREAM_RESULT_BLOCKING;
    }

    if (istream_check_direct(&p->output, type))
        /* already supported by handler (maybe already a pipe) - no
           need for wrapping it into a pipe */
        return istream_invoke_direct(&p->output, type, fd, max_length);

    assert((type & ISTREAM_TO_PIPE) == type);

    if (p->fds[1] < 0 && !p->Create())
        return ISTREAM_RESULT_CLOSED;

    ssize_t nbytes = splice(fd, nullptr, p->fds[1], nullptr, max_length,
                            SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
    /* don't check EAGAIN here (and don't return -2).  We assume that
       splicing to the pipe cannot possibly block, since we flushed
       the pipe; assume that it can only be the source file which is
       blocking */
    if (nbytes <= 0)
        return nbytes;

    assert(p->piped == 0);
    p->piped = (size_t)nbytes;

    if (p->Consume() == ISTREAM_RESULT_CLOSED)
        return ISTREAM_RESULT_CLOSED;

    return nbytes;
}

static void
pipe_input_eof(void *ctx)
{
    PipeIstream *p = (PipeIstream *)ctx;

    p->input.Clear();

    if (p->stock == nullptr && p->fds[1] >= 0) {
        close(p->fds[1]);
        p->fds[1] = -1;
    }

    if (p->piped == 0) {
        p->CloseInternal();
        istream_deinit_eof(&p->output);
    }
}

static void
pipe_input_abort(GError *error, void *ctx)
{
    PipeIstream *p = (PipeIstream *)ctx;

    p->CloseInternal();

    p->input.Clear();
    istream_deinit_abort(&p->output, error);
}

static const struct istream_handler pipe_input_handler = {
    .data = pipe_input_data,
    .direct = pipe_input_direct,
    .eof = pipe_input_eof,
    .abort = pipe_input_abort,
};


/*
 * istream implementation
 *
 */

static inline PipeIstream *
istream_to_pipe(struct istream *istream)
{
    return &ContainerCast2(*istream, &PipeIstream::output);
}

static off_t
istream_pipe_available(struct istream *istream, bool partial)
{
    PipeIstream *p = istream_to_pipe(istream);

    if (likely(p->input.IsDefined())) {
        off_t available = p->input.GetAvailable(partial);
        if (p->piped > 0) {
            if (available != -1)
                available += p->piped;
            else if (partial)
                available = p->piped;
        }

        return available;
    } else {
        assert(p->piped > 0);

        return p->piped;
    }
}

static void
istream_pipe_read(struct istream *istream)
{
    PipeIstream *p = istream_to_pipe(istream);

    if (p->piped > 0 && (p->Consume() <= 0 || p->piped > 0))
        return;

    /* at this point, the pipe must be flushed - if the pipe is
       flushed, this stream is either closed or there must be an input
       stream */
    assert(p->input.IsDefined());

    auto mask = p->output.handler_direct;
    if (mask & FdType::FD_PIPE)
        /* if the handler supports the pipe, we offer our services */
        mask |= ISTREAM_TO_PIPE;

    p->input.SetDirect(mask);
    p->input.Read();
}

static int
istream_pipe_as_fd(struct istream *istream)
{
    PipeIstream *p = istream_to_pipe(istream);

    if (p->piped > 0)
        /* need to flush the pipe buffer first */
        return -1;

    int fd = p->input.AsFd();
    if (fd >= 0) {
        p->CloseInternal();
        istream_deinit(&p->output);
    }

    return fd;
}

static void
istream_pipe_close(struct istream *istream)
{
    PipeIstream *p = istream_to_pipe(istream);

    p->CloseInternal();

    if (p->input.IsDefined())
        p->input.Close();

    istream_deinit(&p->output);
}

static const struct istream_class istream_pipe = {
    .available = istream_pipe_available,
    .read = istream_pipe_read,
    .as_fd = istream_pipe_as_fd,
    .close = istream_pipe_close,
};


/*
 * constructor
 *
 */

PipeIstream::PipeIstream(struct pool &p, struct istream &_input,
                         Stock *_pipe_stock)
    :input(_input, pipe_input_handler, this),
     stock(_pipe_stock)
{
    istream_init(&output, &istream_pipe, &p);
}

struct istream *
istream_pipe_new(struct pool *pool, struct istream *input,
                 Stock *pipe_stock)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    auto *p = NewFromPool<PipeIstream>(*pool, *pool, *input, pipe_stock);
    return &p->output;
}

#endif
