/*
 * Web Application Socket protocol, input data channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was-input.h"
#include "pevent.h"
#include "direct.h"
#include "istream-internal.h"
#include "fifo-buffer.h"
#include "buffered-io.h"
#include "fd-util.h"

#include <daemon/log.h>
#include <was/protocol.h>

#include <errno.h>
#include <string.h>

struct was_input {
    struct istream output;

    int fd;
    struct event event;

    const struct was_input_handler *handler;
    void *handler_ctx;

    struct fifo_buffer *buffer;

    uint64_t received, guaranteed, length;

    bool closed, timeout, known_length;
};

static const struct timeval was_input_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

static void
was_input_schedule_read(struct was_input *input)
{
    assert(input->fd >= 0);
    assert(input->buffer == NULL || !fifo_buffer_full(input->buffer));

    p_event_add(&input->event,
                input->timeout ? &was_input_timeout : NULL,
                input->output.pool, "was_input");
}

static void
was_input_abort(struct was_input *input)
{
    p_event_del(&input->event, input->output.pool);

    /* protect against recursive was_input_free() call within the
       istream handler */
    input->closed = true;

    istream_deinit_abort(&input->output);

    input->handler->abort(input->handler_ctx);
}

static void
was_input_eof(struct was_input *input)
{
    assert(input->known_length);
    assert(input->received == input->length);

    p_event_del(&input->event, input->output.pool);

    istream_deinit_eof(&input->output);

    input->handler->eof(input->handler_ctx);
}

static bool
was_input_check_eof(struct was_input *input)
{
    if (input->known_length && input->received >= input->length &&
        (input->buffer == NULL || fifo_buffer_empty(input->buffer))) {
        was_input_eof(input);
        return true;
    } else
        return false;
}

/**
 * Consume data from the input buffer.  Returns true if data has been
 * consumed.
 */
static bool
was_input_consume_buffer(struct was_input *input)
{
    assert(input->buffer != NULL);

    size_t length;
    const void *p = fifo_buffer_read(input->buffer, &length);
    if (p == NULL)
        return true;

    size_t nbytes = istream_invoke_data(&input->output, p, length);
    if (nbytes == 0)
        return false;

    fifo_buffer_consume(input->buffer, nbytes);

    if (was_input_check_eof(input))
        return false;

    return true;
}


/*
 * socket i/o
 *
 */

static bool
was_input_try_buffered(struct was_input *input)
{
    if (input->buffer == NULL)
        input->buffer = fifo_buffer_new(input->output.pool, 4096);

    size_t max_length = 4096;
    if (input->known_length) {
        uint64_t rest = input->length - input->received;
        if (rest < (uint64_t)max_length)
            max_length = rest;
    }

    ssize_t nbytes = read_to_buffer(input->fd, input->buffer, max_length);
    assert(nbytes != -2);

    if (nbytes == 0) {
        daemon_log(1, "was-input: server closed the data connection\n");
        was_input_abort(input);
        return false;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            was_input_schedule_read(input);
            return true;
        }

        daemon_log(1, "was-input: read error on data connection: %s\n",
                   strerror(errno));
        was_input_abort(input);
        return false;
    }

    input->received += nbytes;

    if (was_input_consume_buffer(input)) {
        assert(!fifo_buffer_full(input->buffer));
        was_input_schedule_read(input);
    }

    return true;
}

static bool
was_input_try_direct(struct was_input *input)
{
    assert(input->buffer == NULL || fifo_buffer_empty(input->buffer));

    size_t max_length = 0x1000000;
    if (input->known_length) {
        uint64_t rest = input->length - input->received;
        if (rest < (uint64_t)max_length)
            max_length = rest;
    }

    ssize_t nbytes = istream_invoke_direct(&input->output, ISTREAM_PIPE,
                                           input->fd, max_length);
    if (nbytes == 0 || nbytes == -2 || nbytes == -3)
        return false;

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            was_input_schedule_read(input);
            return false;
        }

        daemon_log(1, "was-input: read error (%s)\n", strerror(errno));
        was_input_abort(input);
        return false;
    }

    input->received += nbytes;

    if (was_input_check_eof(input))
        return false;

    was_input_schedule_read(input);
    return true;
}

static void
was_input_try_read(struct was_input *input)
{
    if (istream_check_direct(&input->output, ISTREAM_PIPE)) {
        if (input->buffer == NULL || was_input_consume_buffer(input))
            was_input_try_direct(input);
    } else {
        was_input_try_buffered(input);
    }
}


/*
 * libevent callback
 *
 */

static void
was_input_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct was_input *input = ctx;

    assert(input->fd >= 0);

    p_event_consumed(&input->event, input->output.pool);

    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "was-input: data send timeout\n");
        was_input_abort(input);
        return;
    }

    was_input_try_read(input);

    pool_commit();
}


/*
 * istream implementation
 *
 */

static inline struct was_input *
response_stream_to_data(istream_t istream)
{
    return (struct was_input *)(((char*)istream) - offsetof(struct was_input, output));
}

static off_t
was_input_istream_available(istream_t istream, bool partial)
{
    struct was_input *input = response_stream_to_data(istream);

    if (input->known_length)
        return input->length - input->received;
    else if (partial && input->guaranteed > input->received)
        return input->guaranteed - input->received;
    else
        return -1;
}

static void
was_input_istream_read(istream_t istream)
{
    struct was_input *input = response_stream_to_data(istream);

    p_event_del(&input->event, input->output.pool);

    if (input->buffer == NULL || was_input_consume_buffer(input))
        was_input_try_read(input);
}

static void
was_input_istream_close(istream_t istream)
{
    struct was_input *input = response_stream_to_data(istream);

    was_input_abort(input);
}

static const struct istream was_input_stream = {
    .available = was_input_istream_available,
    .read = was_input_istream_read,
    .close = was_input_istream_close,
};


/*
 * constructor
 *
 */

struct was_input *
was_input_new(pool_t pool, int fd,
             const struct was_input_handler *handler, void *handler_ctx)
{
    assert(fd >= 0);
    assert(handler != NULL);
    assert(handler->eof != NULL);
    assert(handler->abort != NULL);

    struct was_input *input = p_malloc(pool, sizeof(*input));
    istream_init(&input->output, &was_input_stream, pool);

    input->fd = fd;
    event_set(&input->event, input->fd, EV_READ|EV_TIMEOUT,
              was_input_event_callback, input);

    input->handler = handler;
    input->handler_ctx = handler_ctx;

    input->buffer = NULL;

    input->received = 0;
    input->guaranteed = 0;
    input->closed = false;
    input->timeout = false;
    input->known_length = false;

    return input;
}

void
was_input_free(struct was_input *input)
{
    p_event_del(&input->event, input->output.pool);

    if (!input->closed)
        istream_deinit_abort(&input->output);
}

istream_t
was_input_enable(struct was_input *input)
{
    was_input_schedule_read(input);
    return istream_struct_cast(&input->output);
}

bool
was_input_set_length(struct was_input *input, uint64_t length)
{
    if (input->known_length) {
        if (length == input->length)
            return true;

        was_input_abort(input);
        return false;
    }

    if (input->guaranteed > length || input->received > length) {
        was_input_abort(input);
        return false;
    }

    input->guaranteed = input->length = length;
    input->known_length = true;

    if (was_input_check_eof(input))
        return false;

    return true;
}
