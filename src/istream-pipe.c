/*
 * Convert any file descriptor to a pipe by splicing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifdef __linux

#include "istream.h"
#include "splice.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

struct istream_pipe {
    struct istream output;
    istream_t input;
    int fds[2];
    size_t piped;
};

static void
pipe_eof_detected(struct istream_pipe *p)
{
    assert(p->input != NULL);
    assert(p->piped == 0);

    pool_unref(p->input->pool);
    p->input = NULL;

    istream_invoke_eof(&p->output);
    istream_close(&p->output);
}

static ssize_t
pipe_consume(struct istream_pipe *p)
{
    ssize_t nbytes;

    assert(p->fds[0] >= 0);
    assert(p->piped > 0);

    nbytes = istream_invoke_direct(&p->output, p->fds[0], p->piped);
    if (unlikely(nbytes < 0 && errno != EAGAIN)) {
        int save_errno = errno;
        istream_close(&p->output);
        errno = save_errno;
        return -1;
    }

    if (nbytes > 0) {
        assert((size_t)nbytes < p->piped);
        p->piped -= (size_t)nbytes;
    }

    return nbytes;
}

static size_t
pipe_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_pipe *p = ctx;

    if (p->piped > 0) {
        pipe_consume(p);
        if (p->piped > 0)
            return 0;
    }

    assert(p->piped == 0);

    return istream_invoke_data(&p->output, data, length);
}

static ssize_t
pipe_input_direct(int fd, size_t max_length, void *ctx)
{
    struct istream_pipe *p = ctx;
    ssize_t nbytes;

    assert(p->fds[1] >= 0);

    if (p->piped > 0) {
        pipe_consume(p);
        if (p->piped > 0)
            return 0;
    }

    nbytes = splice(fd, NULL, p->fds[1], NULL, max_length,
                    SPLICE_F_NONBLOCK | SPLICE_F_MORE | SPLICE_F_MOVE);
    if (unlikely(nbytes < 0 && errno == EAGAIN))
        return -2;

    if (nbytes <= 0)
        return nbytes;

    assert(p->piped == 0);
    p->piped = (size_t)nbytes;

    return pipe_consume(p);
}

static void
pipe_input_eof(void *ctx)
{
    struct istream_pipe *p = ctx;

    p->input = NULL;

    if (p->piped == 0)
        pipe_eof_detected(p);
}

static void
pipe_input_free(void *ctx)
{
    struct istream_pipe *p = ctx;

    if (p->input != NULL) {
        pool_unref(p->input->pool);
        p->input = NULL;

        istream_close(&p->output);
    }
}

static const struct istream_handler pipe_input_handler = {
    .data = pipe_input_data,
    .direct = pipe_input_direct,
    .eof = pipe_input_eof,
    .free = pipe_input_free,
};


static inline struct istream_pipe *
istream_to_pipe(istream_t istream)
{
    return (struct istream_pipe *)(((char*)istream) - offsetof(struct istream_pipe, output));
}

static void
istream_pipe_read(istream_t istream)
{
    struct istream_pipe *p = istream_to_pipe(istream);

    istream_read(p->input);
}

static void
istream_pipe_direct(istream_t istream)
{
    struct istream_pipe *p = istream_to_pipe(istream);

    istream_direct(p->input);
}

static void
istream_pipe_close(istream_t istream)
{
    struct istream_pipe *p = istream_to_pipe(istream);

    if (p->input != NULL) {
        pool_t pool = p->input->pool;
        istream_free(&p->input);
        pool_unref(pool);
    }

    if (p->fds[0] >= 0) {
        close(p->fds[0]);
        p->fds[0] = -1;
    }

    if (p->fds[1] >= 0) {
        close(p->fds[1]);
        p->fds[1] = -1;
    }
    
    istream_invoke_free(&p->output);
}

static const struct istream istream_pipe = {
    .read = istream_pipe_read,
    .direct = istream_pipe_direct,
    .close = istream_pipe_close,
};

istream_t
istream_pipe_new(pool_t pool, istream_t input)
{
    struct istream_pipe *p = p_malloc(pool, sizeof(*p));
    int ret;

    assert(input != NULL);
    assert(input->handler == NULL);
    assert(input->direct != NULL);

    ret = pipe(p->fds);
    if (ret < 0) {
        perror("pipe() failed");
        return NULL;
    }

    p->output = istream_pipe;
    p->output.pool = pool;
    p->input = input;
    p->piped = 0;

    input->handler = &pipe_input_handler;
    input->handler_ctx = p;
    pool_ref(input->pool);

    return &p->output;
}

#endif
