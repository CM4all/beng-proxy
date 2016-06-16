/*
 * Web Application Socket protocol, control channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_control.hxx"
#include "was_quark.h"
#include "buffered_io.hxx"
#include "event/Callback.hxx"
#include "strmap.hxx"
#include "fb_pool.hxx"
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

WasControl::WasControl(int _fd, WasControlHandler &_handler)
    :fd(_fd), handler(_handler),
     input_buffer(fb_pool_get()),
     output_buffer(fb_pool_get())
{
    input.event.Set(fd, EV_READ|EV_TIMEOUT,
                    MakeEventCallback(WasControl, ReadEventCallback),
                    this);
    output.event.Set(fd, EV_WRITE|EV_TIMEOUT,
                     MakeEventCallback(WasControl, WriteEventCallback),
                     this);
    ScheduleRead();
}

void
WasControl::ScheduleRead()
{
    assert(fd >= 0);

    input.event.Add(input_buffer.IsEmpty()
                    ? nullptr : &was_control_timeout);
}

void
WasControl::ScheduleWrite()
{
    assert(fd >= 0);

    output.event.Add(was_control_timeout);
}

void
WasControl::ReleaseSocket()
{
    assert(fd >= 0);

    input_buffer.Free(fb_pool_get());
    output_buffer.Free(fb_pool_get());

    input.event.Delete();
    output.event.Delete();

    fd = -1;
}

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

        input_buffer.Consume(sizeof(*header) + header->length);

        bool success = handler.OnWasControlPacket(was_command(header->command),
                                                  {payload, header->length});
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
    assert(IsDefined());

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
    assert(IsDefined());

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
        output.event.Delete();

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
}

inline void
WasControl::WriteEventCallback(gcc_unused evutil_socket_t _fd, short events)
{
    assert(fd >= 0);
    assert(!output_buffer.IsEmpty());

    if (unlikely(events & EV_TIMEOUT)) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "control send timeout");
        InvokeError(error);
        return;
    }

    TryWrite();
}


/*
 * constructor
 *
 */

void *
WasControl::Start(enum was_command cmd, size_t payload_length)
{
    assert(!done);

    auto w = output_buffer.Write().ToVoid();
    struct was_header *header = (struct was_header *)w.data;
    if (w.size < sizeof(*header) + payload_length) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "control output is too large");
        InvokeError(error);
        return nullptr;
    }

    header->command = cmd;
    header->length = payload_length;

    return header + 1;
}

bool
WasControl::Finish(size_t payload_length)
{
    assert(!done);

    output_buffer.Append(sizeof(struct was_header) + payload_length);
    return output.bulk > 0 || TryWrite();
}

bool
WasControl::Send(enum was_command cmd,
                 const void *payload, size_t payload_length)
{
    assert(!done);

    void *dest = Start(cmd, payload_length);
    if (dest == nullptr)
        return false;

    memcpy(dest, payload, payload_length);
    return Finish(payload_length);
}

bool
WasControl::SendString(enum was_command cmd, const char *payload)
{
    assert(payload != nullptr);

    return Send(cmd, payload, strlen(payload));
}

bool
WasControl::SendArray(enum was_command cmd, ConstBuffer<const char *> values)
{
    for (auto value : values) {
        assert(value != nullptr);

        if (!SendString(cmd, value))
            return false;
    }

    return true;
}

bool
WasControl::SendStrmap(enum was_command cmd, const struct strmap &map)
{
    for (const auto &i : map) {
        size_t key_length = strlen(i.key);
        size_t value_length = strlen(i.value);
        size_t payload_length = key_length + 1 + value_length;

        uint8_t *dest = (uint8_t *)Start(cmd, payload_length);
        if (dest == nullptr)
            return false;

        memcpy(dest, i.key, key_length);
        dest[key_length] = '=';
        memcpy(dest + key_length + 1, i.value, value_length);
        if (!Finish(payload_length))
            return false;
    }

    return true;
}

bool
WasControl::BulkOff()
{
    assert(output.bulk > 0);

    --output.bulk;
    return output.bulk > 0 || TryWrite();
}

void
WasControl::Done()
{
    assert(!done);

    done = true;

    if (!input_buffer.IsEmpty()) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "received too much control data");
        InvokeError(error);
        return;
    }

    if (output_buffer.IsEmpty())
        InvokeDone();
}
