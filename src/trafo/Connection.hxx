/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk.com>
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

#ifndef TRAFO_CONNECTION_HXX
#define TRAFO_CONNECTION_HXX

#include "event/SocketEvent.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/DynamicFifoBuffer.hxx"
#include "AllocatedRequest.hxx"

#include <string>

#include <stdint.h>

enum class TranslationCommand : uint16_t;
class TrafoResponse;
class TrafoListener;
class TrafoHandler;

class TrafoConnection {
    TrafoListener &listener;
    TrafoHandler &handler;

    const UniqueSocketDescriptor fd;
    SocketEvent read_event, write_event;

    enum class State {
        INIT,
        REQUEST,
        PROCESSING,
        RESPONSE,
    } state;

    DynamicFifoBuffer<uint8_t> input;

    AllocatedTrafoRequest request;

    uint8_t *response;

    WritableBuffer<uint8_t> output;

public:
    TrafoConnection(EventLoop &event_loop,
                    TrafoListener &_listener, TrafoHandler &_handler,
                    UniqueSocketDescriptor &&_fd);
    ~TrafoConnection();

    /**
     * For TrafoListener::connections.
     */
    bool operator==(const TrafoConnection &other) const {
        return fd == other.fd;
    }

    void SendResponse(TrafoResponse &&response);

private:
    void TryRead();
    void OnReceived();
    void OnPacket(TranslationCommand cmd, const void *payload, size_t length);

    void TryWrite();

    void ReadEventCallback(unsigned) {
        TryRead();
    }

    void WriteEventCallback(unsigned) {
        TryWrite();
    }
};

#endif
