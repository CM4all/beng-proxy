/*
 * Web Application Socket protocol, output data channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was-output.h"
#include "was-quark.h"
#include "pevent.h"
#include "direct.h"
#include "fd-util.h"
#include "istream.h"

#include <daemon/log.h>
#include <was/protocol.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

struct was_output {
    struct pool *pool;

    int fd;
    struct event event;

    const struct was_output_handler *handler;
    void *handler_ctx;

    struct istream *input;

    uint64_t sent;

    bool known_length;
};

static const struct timeval was_output_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

static void
was_output_schedule_write(struct was_output *output)
{
    p_event_add(&output->event, &was_output_timeout,
                output->pool, "was_output");
}

static void
was_output_abort(struct was_output *output, GError *error)
{
    p_event_del(&output->event, output->pool);

    if (output->input != NULL)
        istream_free_handler(&output->input);

    output->handler->abort(output->handler_ctx, error);
}


/*
 * libevent callback
 *
 */

static void
was_output_event_callback(gcc_unused int fd, short event, void *ctx)
{
    struct was_output *output = ctx;

    assert(output->fd >= 0);
    assert(output->input != NULL);

    p_event_consumed(&output->event, output->pool);

    if (unlikely(event & EV_TIMEOUT)) {
        GError *error = g_error_new_literal(was_quark(), 0, "send timeout");
        was_output_abort(output, error);
        return;
    }

    if (!output->known_length) {
        off_t available = istream_available(output->input, false);
        if (available != -1) {
            output->known_length = true;
            if (!output->handler->length(output->sent + available,
                                         output->handler_ctx))
                return;
        }
    }

    istream_read(output->input);

    pool_commit();
}


/*
 * istream handler for the request
 *
 */

static size_t
was_output_stream_data(const void *p, size_t length, void *ctx)
{
    struct was_output *output = ctx;

    assert(output->fd >= 0);
    assert(output->input != NULL);

    ssize_t nbytes = write(output->fd, p, length);
    if (likely(nbytes > 0)) {
        output->sent += nbytes;
        was_output_schedule_write(output);
    } else if (nbytes < 0) {
        if (errno == EAGAIN) {
            was_output_schedule_write(output);
            return 0;
        }

        GError *error = g_error_new(was_quark(), errno,
                                    "data write failed: %s", strerror(errno));
        was_output_abort(output, error);
        return 0;
    }

    return (size_t)nbytes;
}

static ssize_t
was_output_stream_direct(istream_direct_t type, int fd,
                         size_t max_length, void *ctx)
{
    struct was_output *output = ctx;
    ssize_t nbytes;

    assert(output->fd >= 0);

    nbytes = istream_direct_to_pipe(type, fd, output->fd, max_length);
    if (likely(nbytes > 0)) {
        output->sent += nbytes;
        was_output_schedule_write(output);
    } else if (nbytes < 0 && errno == EAGAIN) {
        if (!fd_ready_for_writing(output->fd)) {
            was_output_schedule_write(output);
            return -2;
        }

        /* try again, just in case output->fd has become ready between
           the first istream_direct_to_pipe() call and
           fd_ready_for_writing() */
        nbytes = istream_direct_to_pipe(type, fd, output->fd, max_length);
    }

    return nbytes;
}

static void
was_output_stream_eof(void *ctx)
{
    struct was_output *output = ctx;

    assert(output->input != NULL);

    output->input = NULL;
    p_event_del(&output->event, output->pool);

    if (!output->known_length &&
        !output->handler->length(output->sent, output->handler_ctx))
        return;

    output->handler->eof(output->handler_ctx);
}

static void
was_output_stream_abort(GError *error, void *ctx)
{
    struct was_output *output = ctx;

    assert(output->input != NULL);

    output->input = NULL;

    output->handler->premature(output->sent, error, output->handler_ctx);
}

static const struct istream_handler was_output_stream_handler = {
    .data = was_output_stream_data,
    .direct = was_output_stream_direct,
    .eof = was_output_stream_eof,
    .abort = was_output_stream_abort,
};


/*
 * constructor
 *
 */

struct was_output *
was_output_new(struct pool *pool, int fd, struct istream *input,
               const struct was_output_handler *handler, void *handler_ctx)
{
    assert(fd >= 0);
    assert(input != NULL);
    assert(handler != NULL);
    assert(handler->length != NULL);
    assert(handler->premature != NULL);
    assert(handler->eof != NULL);
    assert(handler->abort != NULL);

    struct was_output *output = p_malloc(pool, sizeof(*output));
    output->pool = pool;
    output->fd = fd;
    event_set(&output->event, output->fd, EV_WRITE|EV_TIMEOUT,
              was_output_event_callback, output);

    output->handler = handler;
    output->handler_ctx = handler_ctx;

    istream_assign_handler(&output->input, input,
                           &was_output_stream_handler, output,
                           ISTREAM_TO_PIPE);

    output->sent = 0;
    output->known_length = false;

    was_output_schedule_write(output);

    return output;
}

uint64_t
was_output_free(struct was_output *output)
{
    assert(output != NULL);

    if (output->input != NULL)
        istream_free_handler(&output->input);

    p_event_del(&output->event, output->pool);

    return output->sent;
}
