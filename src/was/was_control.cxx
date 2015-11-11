/*
 * Web Application Socket protocol, control channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_control.hxx"
#include "was_quark.h"
#include "buffered_io.hxx"
#include "event/Callback.hxx"
#include "pevent.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>
#include <was/protocol.h>

#include <errno.h>
#include <string.h>

#include <stdio.h>
#include <unistd.h>

static constexpr struct timeval was_control_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

struct WasControl {
    struct pool *pool;

    int fd;

    bool done;

    WasControlHandler &handler;

    struct {
        struct event event;
    } input;

    struct {
        struct event event;
        unsigned bulk;
    } output;

    SliceFifoBuffer input_buffer, output_buffer;

    WasControl(struct pool &_pool, WasControlHandler &_handler)
        :pool(&_pool), handler(_handler),
         input_buffer(fb_pool_get()),
         output_buffer(fb_pool_get()) {}

    void ScheduleRead() {
        assert(fd >= 0);

        p_event_add(&input.event,
                    input_buffer.IsEmpty()
                    ? nullptr : &was_control_timeout,
                    pool, "was_control_input");
    }

    void ScheduleWrite() {
        assert(fd >= 0);

        p_event_add(&output.event, &was_control_timeout,
                    pool, "was_control_output");
    }

    /**
     * Release the socket held by this object.
     */
    void ReleaseSocket() {
        assert(fd >= 0);

        input_buffer.Free(fb_pool_get());
        output_buffer.Free(fb_pool_get());

        p_event_del(&input.event, pool);
        p_event_del(&output.event, pool);

#ifndef NDEBUG
        fd = -1;
#endif
    }

    void InvokeDone() {
        ReleaseSocket();
        handler.OnWasControlDone();
    }

    void InvokeError(GError *error) {
        assert(error != nullptr);

        ReleaseSocket();
        handler.OnWasControlError(error);
    }

    bool InvokeDrained() {
        return handler.OnWasControlDrained();
    }

    /**
     * Consume data from the input buffer.  Returns false if this object
     * has been destructed.
     */
    bool ConsumeInput();

    void TryRead();
    bool TryWrite();

    void ReadEventCallback(evutil_socket_t _fd, short events);
    void WriteEventCallback(evutil_socket_t _fd, short events);
};

bool
WasControl::ConsumeInput()
{
    while (true) {
        auto r = input_buffer.Read().ToVoid();
        const auto header = (const struct was_header *)r.data;
        if (r.size < sizeof(*header))
            /* not enough data yet */
            return InvokeDrained();

        if (r.size < sizeof(*header) + header->length) {
            /* not enough data yet */

            if (input_buffer.IsFull()) {
                GError *error = g_error_new(was_quark(), 0,
                                            "control header too long (%u)",
                                            header->length);
                InvokeError(error);
                return false;
            }

            return InvokeDrained();
        }

        const void *payload = header + 1;

#ifndef NDEBUG
        PoolNotify notify(*pool);
#endif

        input_buffer.Consume(sizeof(*header) + header->length);

        bool success = handler.OnWasControlPacket(was_command(header->command),
                                                  {payload, header->length});
        assert(!notify.Denotify() || !success);

        if (!success)
            return false;
    }
}


/*
 * socket i/o
 *
 */

void
WasControl::TryRead()
{
    ssize_t nbytes = recv_to_buffer(fd, input_buffer, 0xffff);
    assert(nbytes != -2);

    if (nbytes == 0) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "server closed the control connection");
        InvokeError(error);
        return;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            ScheduleRead();
            return;
        }

        GError *error =
            g_error_new(was_quark(), 0,
                        "control receive error: %s", strerror(errno));
        InvokeError(error);
        return;
    }

    if (ConsumeInput()) {
        assert(!input_buffer.IsDefinedAndFull());
        ScheduleRead();
    }
}

bool
WasControl::TryWrite()
{
    ssize_t nbytes = send_from_buffer(fd, output_buffer);
    assert(nbytes != -2);

    if (nbytes == 0) {
        ScheduleWrite();
        return true;
    }

    if (nbytes < 0) {
        GError *error =
            g_error_new(was_quark(), 0,
                        "control send error: %s", strerror(errno));
        InvokeError(error);
        return false;
    }

    if (!output_buffer.IsEmpty())
        ScheduleWrite();
    else if (done) {
        InvokeDone();
        return false;
    } else
        p_event_del(&output.event, pool);

    return true;
}

/*
 * libevent callback
 *
 */

inline void
WasControl::ReadEventCallback(gcc_unused evutil_socket_t _fd, short events)
{
    assert(fd >= 0);

    p_event_consumed(&input.event, pool);

    if (done) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "received too much control data");
        InvokeError(error);
        return;
    }

    if (unlikely(events & EV_TIMEOUT)) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "control receive timeout");
        InvokeError(error);
        return;
    }

    TryRead();

    pool_commit();
}

inline void
WasControl::WriteEventCallback(gcc_unused evutil_socket_t _fd, short events)
{
    assert(fd >= 0);
    assert(!output_buffer.IsEmpty());

    p_event_consumed(&output.event, pool);

    if (unlikely(events & EV_TIMEOUT)) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "control send timeout");
        InvokeError(error);
        return;
    }

    TryWrite();

    pool_commit();
}


/*
 * constructor
 *
 */

WasControl *
was_control_new(struct pool *pool, int fd, WasControlHandler &handler)
{
    assert(fd >= 0);

    auto control = NewFromPool<WasControl>(*pool, *pool, handler);
    control->fd = fd;
    control->done = false;

    event_set(&control->input.event, control->fd, EV_READ|EV_TIMEOUT,
              MakeEventCallback(WasControl, ReadEventCallback), control);

    event_set(&control->output.event, control->fd, EV_WRITE|EV_TIMEOUT,
              MakeEventCallback(WasControl, WriteEventCallback), control);

    control->output.bulk = 0;

    control->ScheduleRead();

    return control;
}

bool
was_control_free(WasControl *control)
{
    control->ReleaseSocket();

    return false; // XXX
}

static void *
was_control_start(WasControl *control, enum was_command cmd,
                  size_t payload_length)
{
    assert(!control->done);

    auto w = control->output_buffer.Write().ToVoid();
    struct was_header *header = (struct was_header *)w.data;
    if (w.size < sizeof(*header) + payload_length) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "control output is too large");
        control->InvokeError(error);
        return nullptr;
    }

    header->command = cmd;
    header->length = payload_length;

    return header + 1;
}

static bool
was_control_finish(WasControl *control, size_t payload_length)
{
    assert(!control->done);

    control->output_buffer.Append(sizeof(struct was_header) + payload_length);
    return control->output.bulk > 0 || control->TryWrite();
}

bool
was_control_send(WasControl *control, enum was_command cmd,
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
was_control_send_string(WasControl *control, enum was_command cmd,
                        const char *payload)
{
    assert(payload != nullptr);

    return was_control_send(control, cmd, payload, strlen(payload));
}

bool
was_control_send_array(WasControl *control, enum was_command cmd,
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
was_control_send_strmap(WasControl *control, enum was_command cmd,
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
was_control_bulk_on(WasControl *control)
{
    ++control->output.bulk;
}

bool
was_control_bulk_off(WasControl *control)
{
    assert(control->output.bulk > 0);

    --control->output.bulk;
    return control->output.bulk > 0 || control->TryWrite();
}

void
was_control_done(WasControl *control)
{
    assert(!control->done);

    control->done = true;

    if (!control->input_buffer.IsEmpty()) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "received too much control data");
        control->InvokeError(error);
        return;
    }

    if (control->output_buffer.IsEmpty())
        control->InvokeDone();
}

bool
was_control_is_empty(WasControl *control)
{
    return control->input_buffer.IsEmpty() && control->output_buffer.IsEmpty();
}
