/*
 * Convert any file descriptor to a pipe by splicing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifdef __linux

#include "istream_pipe.hxx"
#include "istream_internal.hxx"
#include "fd_util.h"
#include "direct.h"
#include "pipe_stock.hxx"
#include "stock.hxx"
#include "gerrno.h"
#include "util/Cast.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

struct istream_pipe {
    struct istream output;
    struct istream *input;
    Stock *stock;
    StockItem *stock_item;
    int fds[2];
    size_t piped;
};

static void
pipe_close(struct istream_pipe *p)
{
    if (p->stock != nullptr) {
        if (p->stock_item != nullptr)
            /* reuse the pipe only if it's empty */
            stock_put(*p->stock_item, p->piped > 0);
    } else {
        if (p->fds[0] >= 0) {
            close(p->fds[0]);
            p->fds[0] = -1;
        }

        if (p->fds[1] >= 0) {
            close(p->fds[1]);
            p->fds[1] = -1;
        }
    }
}

static void
pipe_abort(struct istream_pipe *p, GError *error)
{
    pipe_close(p);

    if (p->input != nullptr)
        istream_close_handler(p->input);

    istream_deinit_abort(&p->output, error);
}

static ssize_t
pipe_consume(struct istream_pipe *p)
{
    ssize_t nbytes;

    assert(p->fds[0] >= 0);
    assert(p->piped > 0);
    assert(p->stock_item != nullptr || p->stock == nullptr);

    nbytes = istream_invoke_direct(&p->output, ISTREAM_PIPE, p->fds[0], p->piped);
    if (unlikely(nbytes == ISTREAM_RESULT_BLOCKING ||
                 nbytes == ISTREAM_RESULT_CLOSED))
        /* handler blocks (-2) or pipe was closed (-3) */
        return nbytes;

    if (unlikely(nbytes == ISTREAM_RESULT_ERRNO && errno != EAGAIN)) {
        GError *error = new_error_errno_msg("read from pipe failed");
        pipe_abort(p, error);
        return ISTREAM_RESULT_CLOSED;
    }

    if (nbytes > 0) {
        assert((size_t)nbytes <= p->piped);
        p->piped -= (size_t)nbytes;

        if (p->piped == 0 && p->stock != nullptr) {
            /* if the pipe was drained, return it to the stock, to
               make it available to other streams */

            stock_put(*p->stock_item, false);
            p->stock_item = nullptr;
            p->fds[0] = -1;
            p->fds[1] = -1;
        }

        if (p->piped == 0 && p->input == nullptr) {
            /* p->input has already reported EOF, and we have been
               waiting for the pipe buffer to become empty */
            pipe_close(p);
            istream_deinit_eof(&p->output);
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
    struct istream_pipe *p = (struct istream_pipe *)ctx;

    assert(p->output.handler != nullptr);

    if (p->piped > 0) {
        ssize_t nbytes = pipe_consume(p);
        if (nbytes == ISTREAM_RESULT_CLOSED)
            return 0;

        if (p->piped > 0 || p->output.handler == nullptr)
            return 0;
    }

    assert(p->piped == 0);

    return istream_invoke_data(&p->output, data, length);
}

static bool
pipe_create(struct istream_pipe *p)
{
    int ret;

    assert(p->fds[0] < 0);
    assert(p->fds[1] < 0);

    if (p->stock != nullptr) {
        assert(p->stock_item == nullptr);

        GError *error = nullptr;
        p->stock_item = stock_get_now(*p->stock, *p->output.pool, nullptr,
                                      &error);
        if (p->stock_item == nullptr) {
            daemon_log(1, "%s\n", error->message);
            g_error_free(error);
            return false;
        }

        pipe_stock_item_get(p->stock_item, p->fds);
    } else {
        ret = pipe_cloexec_nonblock(p->fds);
        if (ret < 0) {
            daemon_log(1, "pipe() failed: %s\n", strerror(errno));
            return false;
        }
    }

    return true;
}

static ssize_t
pipe_input_direct(istream_direct type, int fd, size_t max_length, void *ctx)
{
    struct istream_pipe *p = (struct istream_pipe *)ctx;
    ssize_t nbytes;

    assert(p->output.handler != nullptr);
    assert(p->output.handler->direct != nullptr);
    assert(istream_check_direct(&p->output, ISTREAM_PIPE));

    if (p->piped > 0) {
        nbytes = pipe_consume(p);
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

    if (p->fds[1] < 0 && !pipe_create(p))
        return ISTREAM_RESULT_CLOSED;

    nbytes = splice(fd, nullptr, p->fds[1], nullptr, max_length,
                    SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
    /* don't check EAGAIN here (and don't return -2).  We assume that
       splicing to the pipe cannot possibly block, since we flushed
       the pipe; assume that it can only be the source file which is
       blocking */
    if (nbytes <= 0)
        return nbytes;

    assert(p->piped == 0);
    p->piped = (size_t)nbytes;

    if (pipe_consume(p) == ISTREAM_RESULT_CLOSED)
        return ISTREAM_RESULT_CLOSED;

    return nbytes;
}

static void
pipe_input_eof(void *ctx)
{
    struct istream_pipe *p = (struct istream_pipe *)ctx;

    p->input = nullptr;

    if (p->stock == nullptr && p->fds[1] >= 0) {
        close(p->fds[1]);
        p->fds[1] = -1;
    }

    if (p->piped == 0) {
        pipe_close(p);
        istream_deinit_eof(&p->output);
    }
}

static void
pipe_input_abort(GError *error, void *ctx)
{
    struct istream_pipe *p = (struct istream_pipe *)ctx;

    pipe_close(p);

    p->input = nullptr;
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

static inline struct istream_pipe *
istream_to_pipe(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_pipe::output);
}

static off_t
istream_pipe_available(struct istream *istream, bool partial)
{
    struct istream_pipe *p = istream_to_pipe(istream);

    if (likely(p->input != nullptr)) {
        off_t available = istream_available(p->input, partial);
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
    struct istream_pipe *p = istream_to_pipe(istream);
    istream_direct_t mask;

    if (p->piped > 0 && (pipe_consume(p) <= 0 || p->piped > 0))
        return;

    /* at this point, the pipe must be flushed - if the pipe is
       flushed, this stream is either closed or there must be an input
       stream */
    assert(p->input != nullptr);

    mask = p->output.handler_direct;
    if (mask & ISTREAM_PIPE)
        /* if the handler supports the pipe, we offer our services */
        mask |= ISTREAM_TO_PIPE;

    istream_handler_set_direct(p->input, mask);
    istream_read(p->input);
}

static int
istream_pipe_as_fd(struct istream *istream)
{
    struct istream_pipe *p = istream_to_pipe(istream);

    if (p->piped > 0)
        /* need to flush the pipe buffer first */
        return -1;

    int fd = istream_as_fd(p->input);
    if (fd >= 0) {
        pipe_close(p);
        istream_deinit(&p->output);
    }

    return fd;
}

static void
istream_pipe_close(struct istream *istream)
{
    struct istream_pipe *p = istream_to_pipe(istream);

    pipe_close(p);

    if (p->input != nullptr)
        istream_close_handler(p->input);

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

struct istream *
istream_pipe_new(struct pool *pool, struct istream *input,
                 Stock *pipe_stock)
{
    struct istream_pipe *p = istream_new_macro(pool, pipe);

    assert(input != nullptr);
    assert(!istream_has_handler(input));

    p->stock = pipe_stock;
    p->stock_item = nullptr;
    p->fds[0] = -1;
    p->fds[1] = -1;
    p->piped = 0;

    istream_assign_handler(&p->input, input,
                           &pipe_input_handler, p,
                           0);

    return &p->output;
}

#endif
