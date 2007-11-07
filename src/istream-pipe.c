/*
 * Convert any file descriptor to a pipe by splicing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifdef __linux

#include "istream.h"
#include "splice.h"

#include <daemon/log.h>

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* XXX ISTREAM_SOCKET is not yet supported by Linux 2.6.23 */
#define SPLICE_SOURCE_TYPES (ISTREAM_FILE | ISTREAM_PIPE)

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
        istream_invoke_abort(&p->output);
    else
        istream_free_unref(&p->input);
}

static ssize_t
pipe_consume(struct istream_pipe *p)
{
    ssize_t nbytes;

    assert(p->fds[0] >= 0);
    assert(p->piped > 0);

    nbytes = istream_invoke_direct(&p->output, ISTREAM_PIPE, p->fds[0], p->piped);
    if (unlikely(nbytes < 0 && errno != EAGAIN)) {
        int save_errno = errno;
        pipe_abort(p);
        errno = save_errno;
        return -1;
    }

    if (nbytes > 0) {
        assert((size_t)nbytes < p->piped);
        p->piped -= (size_t)nbytes;

        if (p->piped == 0 && p->input == NULL) {
            /* p->input has already reported EOF, and we have been
               waiting for the pipe buffer to become empty */
            istream_invoke_eof(&p->output);
            pipe_close(p);
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
        pipe_consume(p);
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
        pipe_consume(p);
        if (p->piped > 0 || p->output.handler == NULL)
            return 0;
    }

    if ((p->output.handler_direct & type) != 0)
        /* already supported by handler (maybe already a pipe) - no
           need for wrapping it into a pipe */
        return istream_invoke_direct(&p->output, type, fd, max_length);

    assert((type & SPLICE_SOURCE_TYPES) == type);

    if (p->fds[1] < 0) {
        int ret;

        assert(p->fds[0] < 0);

        ret = pipe(p->fds);
        if (ret < 0) {
            daemon_log(1, "pipe() failed: %s\n", strerror(errno));
            pipe_close(p);
            return 0;
        }
    }

    nbytes = splice(fd, NULL, p->fds[1], NULL, max_length,
                    SPLICE_F_NONBLOCK | SPLICE_F_MORE | SPLICE_F_MOVE);
    if (unlikely(nbytes < 0 && errno == EAGAIN))
        return -2;

    if (nbytes <= 0)
        return nbytes;

    assert(p->piped == 0);
    p->piped = (size_t)nbytes;

    pipe_consume(p);

    return nbytes;
}

static void
pipe_input_eof(void *ctx)
{
    struct istream_pipe *p = ctx;

    istream_clear_unref_handler(&p->input);

    if (p->fds[1] >= 0) {
        close(p->fds[1]);
        p->fds[1] = -1;
    }

    if (p->piped == 0) {
        pool_ref(p->output.pool);
        istream_invoke_eof(&p->output);
        pipe_close(p);
        pool_unref(p->output.pool);
    }
}

static void
pipe_input_abort(void *ctx)
{
    struct istream_pipe *p = ctx;

    if (p->input != NULL)
        istream_clear_unref(&p->input);

    pipe_close(p);

    istream_invoke_abort(&p->output);
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

static void
istream_pipe_read(istream_t istream)
{
    struct istream_pipe *p = istream_to_pipe(istream);

    /* XXX is this update required? */
    istream_handler_set_direct(p->input,
                               p->output.handler_direct | SPLICE_SOURCE_TYPES);

    if (unlikely(p->input == NULL)) {
        assert(p->piped > 0);

        pipe_consume(p);
    } else {
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
    struct istream_pipe *p = p_malloc(pool, sizeof(*p));

    assert(input != NULL);
    assert(!istream_has_handler(input));

    p->output = istream_pipe;
    p->output.pool = pool;
    p->fds[0] = -1;
    p->fds[1] = -1;
    p->piped = 0;

    istream_assign_ref_handler(&p->input, input,
                               &pipe_input_handler, p,
                               SPLICE_SOURCE_TYPES);

    return istream_struct_cast(&p->output);
}

#endif
