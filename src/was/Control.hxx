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

#ifndef BENG_PROXY_WAS_CONTROL_HXX
#define BENG_PROXY_WAS_CONTROL_HXX

#include "event/SocketEvent.hxx"
#include "SliceFifoBuffer.hxx"

#include <exception>

#include <was/protocol.h>

#include <stddef.h>

class StringMap;
template<typename T> struct ConstBuffer;

class WasControlHandler {
public:
    /**
     * A packet was received.
     *
     * @return false if the object was closed
     */
    virtual bool OnWasControlPacket(enum was_command cmd,
                                    ConstBuffer<void> payload) = 0;

    /**
     * Called after a group of control packets have been handled, and
     * the input buffer is drained.
     *
     * @return false if the #WasControl object has been destructed
     */
    virtual bool OnWasControlDrained() {
        return true;
    }

    virtual void OnWasControlDone() = 0;
    virtual void OnWasControlError(std::exception_ptr ep) = 0;
};

/**
 * Web Application Socket protocol, control channel library.
 */
class WasControl {
    int fd;

    bool done = false;

    WasControlHandler &handler;

    SocketEvent read_event, write_event;

    struct {
        unsigned bulk = 0;
    } output;

    SliceFifoBuffer input_buffer, output_buffer;

public:
    WasControl(EventLoop &event_loop, int _fd, WasControlHandler &_handler);

    EventLoop &GetEventLoop() {
        return read_event.GetEventLoop();
    }

    bool IsDefined() const {
        return fd >= 0;
    }

    bool Send(enum was_command cmd,
              const void *payload, size_t payload_length);

    bool SendEmpty(enum was_command cmd) {
        return Send(cmd, nullptr, 0);
    }

    bool SendString(enum was_command cmd, const char *payload);

    bool SendUint64(enum was_command cmd, uint64_t payload) {
        return Send(cmd, &payload, sizeof(payload));
    }

    bool SendArray(enum was_command cmd, ConstBuffer<const char *> values);

    bool SendStrmap(enum was_command cmd, const StringMap &map);

    /**
     * Enables bulk mode.
     */
    void BulkOn() {
        ++output.bulk;
    }

    /**
     * Disables bulk mode and flushes the output buffer.
     */
    bool BulkOff();

    void Done();

    bool IsEmpty() const {
        return input_buffer.IsEmpty() && output_buffer.IsEmpty();
    }

private:
    void *Start(enum was_command cmd, size_t payload_length);
    bool Finish(size_t payload_length);

    void ScheduleRead();
    void ScheduleWrite();

public:
    /**
     * Release the socket held by this object.
     */
    void ReleaseSocket();

private:
    void InvokeDone() {
        ReleaseSocket();
        handler.OnWasControlDone();
    }

    void InvokeError(std::exception_ptr ep) {
        ReleaseSocket();
        handler.OnWasControlError(ep);
    }

    void InvokeError(const char *msg);

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

    void ReadEventCallback(unsigned events);
    void WriteEventCallback(unsigned events);
};

#endif
