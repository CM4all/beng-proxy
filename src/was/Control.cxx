/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Control.hxx"
#include "Error.hxx"
#include "strmap.hxx"
#include "fb_pool.hxx"
#include "net/Buffered.hxx"
#include "system/Error.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringFormat.hxx"

#include <was/protocol.h>

#include <errno.h>
#include <string.h>

#include <stdio.h>
#include <unistd.h>

static constexpr struct timeval was_control_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

WasControl::WasControl(EventLoop &event_loop, int _fd,
                       WasControlHandler &_handler)
    :fd(_fd), handler(_handler),
     read_event(event_loop, fd, SocketEvent::READ,
                BIND_THIS_METHOD(ReadEventCallback)),
     write_event(event_loop, fd, SocketEvent::WRITE,
                 BIND_THIS_METHOD(WriteEventCallback)),
     input_buffer(fb_pool_get()),
     output_buffer(fb_pool_get())
{
    ScheduleRead();
}

void
WasControl::ScheduleRead()
{
    assert(fd >= 0);

    read_event.Add(input_buffer.IsEmpty()
                   ? nullptr : &was_control_timeout);
}

void
WasControl::ScheduleWrite()
{
    assert(fd >= 0);

    write_event.Add(was_control_timeout);
}

void
WasControl::ReleaseSocket()
{
    assert(fd >= 0);

    input_buffer.Free(fb_pool_get());
    output_buffer.Free(fb_pool_get());

    read_event.Delete();
    write_event.Delete();

    fd = -1;
}

void
WasControl::InvokeError(const char *msg)
{
    InvokeError(std::make_exception_ptr(WasProtocolError(msg)));
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
                InvokeError(std::make_exception_ptr(WasProtocolError(StringFormat<64>("control header too long (%u)",
                                                                                      header->length))));
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

    ssize_t nbytes = ReceiveToBuffer(fd, input_buffer);
    assert(nbytes != -2);

    if (nbytes == 0) {
        InvokeError("server closed the control connection");
        return;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            ScheduleRead();
            return;
        }

        InvokeError(std::make_exception_ptr(MakeErrno("WAS control receive error")));
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

    ssize_t nbytes = SendFromBuffer(fd, output_buffer);
    assert(nbytes != -2);

    if (nbytes == 0) {
        ScheduleWrite();
        return true;
    }

    if (nbytes < 0) {
        InvokeError(std::make_exception_ptr(MakeErrno("WAS control send error")));
        return false;
    }

    if (!output_buffer.IsEmpty())
        ScheduleWrite();
    else if (done) {
        InvokeDone();
        return false;
    } else
        write_event.Delete();

    return true;
}

/*
 * libevent callback
 *
 */

inline void
WasControl::ReadEventCallback(unsigned events)
{
    assert(fd >= 0);

    if (done) {
        InvokeError("received too much control data");
        return;
    }

    if (gcc_unlikely(events & SocketEvent::TIMEOUT)) {
        InvokeError("control receive timeout");
        return;
    }

    TryRead();
}

inline void
WasControl::WriteEventCallback(unsigned events)
{
    assert(fd >= 0);
    assert(!output_buffer.IsEmpty());

    if (gcc_unlikely(events & SocketEvent::TIMEOUT)) {
        InvokeError("control send timeout");
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
        InvokeError("control output is too large");
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
WasControl::SendStrmap(enum was_command cmd, const StringMap &map)
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
        InvokeError("received too much control data");
        return;
    }

    if (output_buffer.IsEmpty())
        InvokeDone();
}
