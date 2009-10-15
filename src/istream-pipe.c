/*
 * Convert any file descriptor to a pipe by splicing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifdef __linux

#include "istream-internal.h"
#include "fd-util.h"
#include "direct.h"

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

struct istream_pipe {
    struct istream output;
    istream_t input;
    int fds[2];
    size_t piped;
};

static void
pipe_close(struct istream_pipe *p)
{
    if (p->fds[0] >= 0) {
        close(p->fds[0]);
        p->fds[0] = -1;
    }

    if (p->fds[1] >= 0) {
        close(p->fds[1]);
        p->fds[1] = -1;
    }
}

static void
pipe_abort(struct istream_pipe *p)
{
    pipe_close(p);

    if (p->input == NULL)
        istream_deinit_abort(&p->output);
    else
        istream_close(p->input);
}

static ssize_t
pipe_consume(struct istream_pipe *p)
{
    ssize_t nbytes;

    assert(p->fds[0] >= 0);
    assert(p->piped > 0);

    nbytes = istream_invoke_direct(&p->output, ISTREAM_PIPE, p->fds[0], p->piped);
    if (unlikely(nbytes == -3))
        return -3;

    if (unlikely(nbytes < 0 && errno != EAGAIN)) {
        int save_errno = errno;
        pipe_abort(p);
        errno = save_errno;
        return -3;
    }

    if (nbytes > 0) {
        assert((size_t)nbytes <= p->piped);
        p->piped -= (size_t)nbytes;

        if (p->piped == 0 && p->input == NULL) {
            /* p->input has already reported EOF, and we have been
               waiting for the pipe buffer to become empty */
            istream_deinit_eof(&p->output);
            pipe_close(p);
            return -3;
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
    struct istream_pipe *p = ctx;

    assert(p->output.handler != NULL);

    if (p->piped > 0) {
        ssize_t nbytes = pipe_consume(p);
        if (nbytes == -3)
            return 0;

        if (p->piped > 0 || p->output.handler == NULL)
            return 0;
    }

    assert(p->piped == 0);

    return istream_invoke_data(&p->output, data, length);
}

static ssize_t
pipe_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_pipe *p = ctx;
    ssize_t nbytes;

    assert(p->output.handler != NULL);
    assert(p->output.handler->direct != NULL);
    assert((p->output.handler_direct & ISTREAM_PIPE) != 0);

    if (p->piped > 0) {
        nbytes = pipe_consume(p);
        if (nbytes <= 0)
            return nbytes;

        if (p->piped > 0)
            /* if the pipe still isn't empty, we can't start reading
               new input */
            return -2;
    }

    if ((p->output.handler_direct & type) != 0)
        /* already supported by handler (maybe already a pipe) - no
           need for wrapping it into a pipe */
        return istream_invoke_direct(&p->output, type, fd, max_length);

    assert((type & ISTREAM_TO_PIPE) == type);

    if (p->fds[1] < 0) {
        int ret;

        assert(p->fds[0] < 0);

        ret = pipe(p->fds);
        if (ret < 0) {
            daemon_log(1, "pipe() failed: %s\n", strerror(errno));
            pipe_close(p);
            return -3;
        }

        fd_set_cloexec(p->fds[0]);
        fd_set_cloexec(p->fds[1]);
    }

    nbytes = splice(fd, NULL, p->fds[1], NULL, max_length,
                    /* SPLICE_F_NONBLOCK | */ SPLICE_F_MORE | SPLICE_F_MOVE);
    /* don't check EAGAIN here (and don't return -2).  We assume that
       splicing to the pipe cannot possibly block, since we flushed
       the pipe; assume that it can only be the source file which is
       blocking */
    if (nbytes <= 0)
        return nbytes;

    assert(p->piped == 0);
    p->piped = (size_t)nbytes;

    if (pipe_consume(p) == -3)
        return -3;

    return nbytes;
}

static void
pipe_input_eof(void *ctx)
{
    struct istream_pipe *p = ctx;

    p->input = NULL;

    if (p->fds[1] >= 0) {
        close(p->fds[1]);
        p->fds[1] = -1;
    }

    if (p->piped == 0) {
        pool_ref(p->output.pool);
        istream_deinit_eof(&p->output);
        pipe_close(p);
        pool_unref(p->output.pool);
    }
}

static void
pipe_input_abort(void *ctx)
{
    struct istream_pipe *p = ctx;

    pipe_close(p);

    p->input = NULL;
    istream_deinit_abort(&p->output);
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
istream_to_pipe(istream_t istream)
{
    return (struct istream_pipe *)(((char*)istream) - offsetof(struct istream_pipe, output));
}

static off_t
istream_pipe_available(istream_t istream, bool partial)
{
    struct istream_pipe *p = istream_to_pipe(istream);

    if (likely(p->input != NULL)) {
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
istream_pipe_read(istream_t istream)
{
    struct istream_pipe *p = istream_to_pipe(istream);

    if (unlikely(p->input == NULL)) {
        assert(p->piped > 0);

        pipe_consume(p);
    } else {
        /* XXX is this update required? */
        istream_handler_set_direct(p->input,
                                   p->output.handler_direct | ISTREAM_TO_PIPE);

        istream_read(p->input);
    }
}

static void
istream_pipe_close(istream_t istream)
{
    struct istream_pipe *p = istream_to_pipe(istream);

    pipe_abort(p);
}

static const struct istream istream_pipe = {
    .available = istream_pipe_available,
    .read = istream_pipe_read,
    .close = istream_pipe_close,
};


/*
 * constructor
 *
 */

istream_t
istream_pipe_new(pool_t pool, istream_t input)
{
    struct istream_pipe *p = istream_new_macro(pool, pipe);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    p->fds[0] = -1;
    p->fds[1] = -1;
    p->piped = 0;

    istream_assign_handler(&p->input, input,
                           &pipe_input_handler, p,
                           ISTREAM_TO_PIPE);

    return istream_struct_cast(&p->output);
}

#endif
