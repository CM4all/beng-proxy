/*
 * Web Application Socket protocol, control channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_control.hxx"
#include "was_quark.h"
#include "fifo-buffer.h"
#include "buffered_io.h"
#include "pevent.h"
#include "strmap.hxx"
#include "pool.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>
#include <was/protocol.h>

#include <errno.h>
#include <string.h>

#include <stdio.h>
#include <unistd.h>

struct was_control {
    struct pool *pool;

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
        unsigned bulk;
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
                ? nullptr : &was_control_timeout,
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
    assert(control != nullptr);
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
was_control_abort(struct was_control *control, GError *error)
{
    assert(error != nullptr);

    was_control_release_socket(control);

    control->handler->abort(error, control->handler_ctx);
}

static bool
was_control_drained(struct was_control *control)
{
    return control->handler->drained == nullptr ||
        control->handler->drained(control->handler_ctx);
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
        if (data == nullptr || length < sizeof(*header))
            /* not enough data yet */
            return was_control_drained(control);

        header = (const struct was_header *)data;
        if (length < sizeof(*header) + header->length) {
            /* not enough data yet */

            if (fifo_buffer_full(control->input.buffer)) {
                GError *error = g_error_new(was_quark(), 0,
                                            "control header too long (%u)",
                                            header->length);
                was_control_abort(control, error);
                return false;
            }

            return was_control_drained(control);
        }

        const void *payload = header + 1;

#ifndef NDEBUG
        struct pool_notify_state notify;
        pool_notify(control->pool, &notify);
#endif

        fifo_buffer_consume(control->input.buffer,
                            sizeof(*header) + header->length);

        if (!control->handler->packet(was_command(header->command),
                                      payload, header->length,
                                      control->handler_ctx))
            return false;

        assert(!pool_denotify(&notify));
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
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "server closed the control connection");
        was_control_abort(control, error);
        return;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            was_control_schedule_read(control);
            return;
        }

        GError *error =
            g_error_new(was_quark(), 0,
                        "control receive error: %s", strerror(errno));
        was_control_abort(control, error);
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

        GError *error =
            g_error_new(was_quark(), 0,
                        "control send error: %s", strerror(errno));
        was_control_abort(control, error);
        return false;
    }

    if (!fifo_buffer_empty(control->output.buffer))
        was_control_schedule_write(control);
    else if (control->done) {
        was_control_eof((struct was_control *)control->handler_ctx);
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
was_control_input_event_callback(int fd gcc_unused, short event, void *ctx)
{
    struct was_control *control = (struct was_control *)ctx;

    assert(control->fd >= 0);

    p_event_consumed(&control->input.event, control->pool);

    if (control->done) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "received too much control data");
        was_control_abort(control, error);
        return;
    }

    if (unlikely(event & EV_TIMEOUT)) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "control receive timeout");
        was_control_abort(control, error);
        return;
    }

    was_control_try_read(control);

    pool_commit();
}

static void
was_control_output_event_callback(int fd gcc_unused, short event, void *ctx)
{
    struct was_control *control = (struct was_control *)ctx;

    assert(control->fd >= 0);
    assert(!fifo_buffer_empty(control->output.buffer));

    p_event_consumed(&control->output.event, control->pool);

    if (unlikely(event & EV_TIMEOUT)) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "control send timeout");
        was_control_abort(control, error);
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
was_control_new(struct pool *pool, int fd,
                const struct was_control_handler *handler,
                void *handler_ctx)
{
    assert(fd >= 0);
    assert(handler != nullptr);
    assert(handler->packet != nullptr);
    assert(handler->eof != nullptr);
    assert(handler->abort != nullptr);

    auto control = NewFromPool<struct was_control>(*pool);
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
    control->output.bulk = 0;

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
    struct was_header *header = (struct was_header *)
        fifo_buffer_write(control->output.buffer, &max_length);
    if (header == nullptr || max_length < sizeof(*header) + payload_length) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "control output is too large");
        was_control_abort(control, error);
        return nullptr;
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
    return control->output.bulk > 0 || was_control_try_write(control);
}

bool
was_control_send(struct was_control *control, enum was_command cmd,
                 const void *payload, size_t payload_length)
{
    assert(!control->done);

    void *dest = was_control_start(control, cmd, payload_length);
    if (dest == nullptr)
        return false;

    memcpy(dest, payload, payload_length);
    return was_control_finish(control, payload_length);
}

bool
was_control_send_string(struct was_control *control, enum was_command cmd,
                        const char *payload)
{
    assert(payload != nullptr);

    return was_control_send(control, cmd, payload, strlen(payload));
}

bool
was_control_send_array(struct was_control *control, enum was_command cmd,
                       ConstBuffer<const char *> values)
{
    assert(control != nullptr);

    for (auto value : values) {
        assert(value != nullptr);

        if (!was_control_send_string(control, cmd, value))
            return false;
    }

    return true;
}

bool
was_control_send_strmap(struct was_control *control, enum was_command cmd,
                        const struct strmap *map)
{
    assert(control != nullptr);
    assert(map != nullptr);

    for (const auto &i : *map) {
        size_t key_length = strlen(i.key);
        size_t value_length = strlen(i.value);
        size_t payload_length = key_length + 1 + value_length;

        uint8_t *dest = (uint8_t *)
            was_control_start(control, cmd, payload_length);
        if (dest == nullptr)
            return false;

        memcpy(dest, i.key, key_length);
        dest[key_length] = '=';
        memcpy(dest + key_length + 1, i.value, value_length);
        if (!was_control_finish(control, payload_length))
            return false;
    }

    return true;
}

void
was_control_bulk_on(struct was_control *control)
{
    ++control->output.bulk;
}

bool
was_control_bulk_off(struct was_control *control)
{
    assert(control->output.bulk > 0);

    --control->output.bulk;
    return control->output.bulk > 0 || was_control_try_write(control);
}

void
was_control_done(struct was_control *control)
{
    assert(!control->done);

    control->done = true;

    if (!fifo_buffer_empty(control->input.buffer)) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "received too much control data");
        was_control_abort(control, error);
        return;
    }

    if (fifo_buffer_empty(control->output.buffer))
        was_control_eof(control);
}

bool
was_control_is_empty(struct was_control *control)
{
    return fifo_buffer_empty(control->input.buffer) &&
        fifo_buffer_empty(control->output.buffer);
}
