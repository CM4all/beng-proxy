/*
 * Web Application Socket protocol, control channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was-control.h"
#include "fifo-buffer.h"
#include "buffered-io.h"
#include "pevent.h"
#include "strmap.h"

#include <daemon/log.h>
#include <was/protocol.h>

#include <errno.h>
#include <string.h>

#include <stdio.h>
#include <unistd.h>

struct was_control {
    pool_t pool;

    int fd;

    bool done;

    const struct was_control_handler *handler;
    void *handler_ctx;

    struct {
        struct event event;
        struct fifo_buffer *buffer;
    } input;

    struct {
        struct event event;
        struct fifo_buffer *buffer;
    } output;
};

static const struct timeval was_control_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

static void
was_control_schedule_read(struct was_control *control)
{
    assert(control->fd >= 0);

    p_event_add(&control->input.event,
                fifo_buffer_empty(control->input.buffer)
                ? NULL : &was_control_timeout,
                control->pool, "was_control_input");
}

static void
was_control_schedule_write(struct was_control *control)
{
    assert(control->fd >= 0);

    p_event_add(&control->output.event, &was_control_timeout,
                control->pool, "was_control_output");
}

/**
 * Release the socket held by this object.
 */
static void
was_control_release_socket(struct was_control *control)
{
    assert(control != NULL);
    assert(control->fd >= 0);

    p_event_del(&control->input.event, control->pool);
    p_event_del(&control->output.event, control->pool);

#ifndef NDEBUG
    control->fd = -1;
#endif
}

static void
was_control_eof(struct was_control *control)
{
    was_control_release_socket(control);

    control->handler->eof(control->handler_ctx);
}

static void
was_control_abort(struct was_control *control)
{
    was_control_release_socket(control);

    control->handler->abort(control->handler_ctx);
}

/**
 * Consume data from the input buffer.  Returns false if this object
 * has been destructed.
 */
static bool
was_control_consume_input(struct was_control *control)
{
    const void *data;
    size_t length;
    const struct was_header *header;

    while (true) {
        data = fifo_buffer_read(control->input.buffer, &length);
        if (data == NULL || length < sizeof(*header))
            /* not enough data yet */
            return true;

        header = data;
        if (length < sizeof(*header) + header->length) {
            /* not enough data yet */

            if (fifo_buffer_full(control->input.buffer)) {
                daemon_log(2, "was-control: header too long (%u)\n",
                           header->length);
                was_control_abort(control);
                return false;
            }

            return true;
        }

        const void *payload = header + 1;

        if (!control->handler->packet(header->command, payload, header->length,
                                      control->handler_ctx))
            return false;

        fifo_buffer_consume(control->input.buffer,
                            sizeof(*header) + header->length);
    }
}


/*
 * socket i/o
 *
 */

static void
was_control_try_read(struct was_control *control)
{
    ssize_t nbytes = recv_to_buffer(control->fd, control->input.buffer,
                                    0xffff);
    assert(nbytes != -2);

    if (nbytes == 0) {
        daemon_log(1, "was-control: server closed the connection\n");
        was_control_abort(control);
        return;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            was_control_schedule_read(control);
            return;
        }

        daemon_log(1, "was-control: receive error: %s\n", strerror(errno));
        was_control_abort(control);
        return;
    }

    if (was_control_consume_input(control)) {
        assert(!fifo_buffer_full(control->input.buffer));
        was_control_schedule_read(control);
    }
}

static bool
was_control_try_write(struct was_control *control)
{
    ssize_t nbytes = send_from_buffer(control->fd, control->output.buffer);
    assert(nbytes != -2);

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            was_control_schedule_write(control);
            return true;
        }

        daemon_log(1, "was-control: send error: %s\n", strerror(errno));
        was_control_abort(control);
        return false;
    }

    if (!fifo_buffer_empty(control->output.buffer))
        was_control_schedule_write(control);
    else if (control->done) {
        was_control_eof(control->handler_ctx);
        return false;
    } else
        p_event_del(&control->output.event, control->pool);

    return true;
}


/*
 * libevent callback
 *
 */

static void
was_control_input_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct was_control *control = ctx;

    assert(control->fd >= 0);

    p_event_consumed(&control->input.event, control->pool);

    if (control->done) {
        daemon_log(2, "was-control: received too much data\n");
        was_control_abort(control);
        return;
    }

    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "was-control: recv timeout\n");
        was_control_abort(control);
        return;
    }

    was_control_try_read(control);

    pool_commit();
}

static void
was_control_output_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct was_control *control = ctx;

    assert(control->fd >= 0);
    assert(!fifo_buffer_empty(control->output.buffer));

    p_event_consumed(&control->output.event, control->pool);

    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "was-control: send timeout\n");
        was_control_abort(control);
        return;
    }

    was_control_try_write(control);

    pool_commit();
}


/*
 * constructor
 *
 */

struct was_control *
was_control_new(pool_t pool, int fd,
                const struct was_control_handler *handler,
                void *handler_ctx)
{
    assert(fd >= 0);
    assert(handler != NULL);
    assert(handler->packet != NULL);
    assert(handler->eof != NULL);
    assert(handler->abort != NULL);

    struct was_control *control = p_malloc(pool, sizeof(*control));
    control->pool = pool;
    control->fd = fd;
    control->done = false;

    control->handler = handler;
    control->handler_ctx = handler_ctx;

    event_set(&control->input.event, control->fd, EV_READ|EV_TIMEOUT,
              was_control_input_event_callback, control);
    control->input.buffer = fifo_buffer_new(pool, 4096);

    event_set(&control->output.event, control->fd, EV_WRITE|EV_TIMEOUT,
              was_control_output_event_callback, control);
    control->output.buffer = fifo_buffer_new(pool, 8192);

    was_control_schedule_read(control);

    return control;
}

bool
was_control_free(struct was_control *control)
{
    was_control_release_socket(control);

    return false; // XXX
}

static void *
was_control_start(struct was_control *control, enum was_command cmd,
                  size_t payload_length)
{
    assert(!control->done);

    size_t max_length;
    struct was_header *header = fifo_buffer_write(control->output.buffer,
                                                  &max_length);
    if (header == NULL || max_length < sizeof(*header) + payload_length) {
        daemon_log(2, "was-control: output is too large\n");
        was_control_abort(control);
        return NULL;
    }

    header->command = cmd;
    header->length = payload_length;

    return header + 1;
}

static bool
was_control_finish(struct was_control *control, size_t payload_length)
{
    assert(!control->done);

    fifo_buffer_append(control->output.buffer,
                       sizeof(struct was_header) + payload_length);
    return was_control_try_write(control);
}

bool
was_control_send(struct was_control *control, enum was_command cmd,
                 const void *payload, size_t payload_length)
{
    assert(!control->done);

    void *dest = was_control_start(control, cmd, payload_length);
    if (dest == NULL)
        return false;

    memcpy(dest, payload, payload_length);
    return was_control_finish(control, payload_length);
}

bool
was_control_send_string(struct was_control *control, enum was_command cmd,
                        const char *payload)
{
    assert(payload != NULL);

    return was_control_send(control, cmd, payload, strlen(payload));
}

bool
was_control_send_strmap(struct was_control *control, enum was_command cmd,
                        struct strmap *map)
{
    assert(control != NULL);
    assert(map != NULL);

    strmap_rewind(map);

    const struct strmap_pair *pair;
    while ((pair = strmap_next(map)) != NULL) {
        size_t key_length = strlen(pair->key);
        size_t value_length = strlen(pair->value);
        size_t payload_length = key_length + 1 + value_length;

        char *dest = was_control_start(control, cmd, payload_length);
        if (dest == NULL)
            return false;

        memcpy(dest, pair->key, key_length);
        dest[key_length] = '=';
        memcpy(dest + key_length + 1, pair->value, value_length);
        if (!was_control_finish(control, payload_length))
            return false;
    }

    return true;
}

void
was_control_done(struct was_control *control)
{
    assert(!control->done);

    control->done = true;

    if (!fifo_buffer_empty(control->input.buffer)) {
        daemon_log(2, "was-control: received too much data\n");
        was_control_abort(control);
        return;
    }

    if (fifo_buffer_empty(control->output.buffer))
        was_control_eof(control);
}
