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

#include "Connection.hxx"
#include "Listener.hxx"
#include "Handler.hxx"
#include "Response.hxx"
#include "translation/Protocol.hxx"

#include <daemon/log.h>

#include <unistd.h>
#include <errno.h>
#include <string.h>

TrafoConnection::TrafoConnection(EventLoop &event_loop,
                                 TrafoListener &_listener,
                                 TrafoHandler &_handler,
                                 UniqueSocketDescriptor &&_fd)
    :listener(_listener), handler(_handler),
     fd(std::move(_fd)),
     read_event(event_loop, fd.Get(), SocketEvent::READ|SocketEvent::PERSIST,
                BIND_THIS_METHOD(ReadEventCallback)),
     write_event(event_loop, fd.Get(), SocketEvent::WRITE|SocketEvent::PERSIST,
                 BIND_THIS_METHOD(WriteEventCallback)),
     state(State::INIT),
     input(8192)
{
    read_event.Add();
}

TrafoConnection::~TrafoConnection()
{
    read_event.Delete();
    write_event.Delete();

    if (state == State::RESPONSE)
        delete[] response;
}

inline void
TrafoConnection::TryRead()
{
    assert(state == State::INIT || state == State::REQUEST);

    auto r = input.Write();
    assert(!r.empty());

    ssize_t nbytes = recv(fd.Get(), r.data, r.size, MSG_DONTWAIT);
    if (gcc_likely(nbytes > 0)) {
        input.Append(nbytes);
        OnReceived();
        return;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN)
            return;

        daemon_log(2, "Failed to read from client: %s\n", strerror(errno));
    }

    listener.RemoveConnection(*this);
}

inline void
TrafoConnection::OnReceived()
{
    assert(state != State::PROCESSING);

    while (true) {
        auto r = input.Read();
        const void *p = r.data;
        const auto *header = (const TranslationHeader *)p;
        if (r.size < sizeof(*header))
            break;

        const size_t payload_length = header->length;
        const size_t total_size = sizeof(*header) + payload_length;
        if (r.size < total_size)
            break;

        OnPacket(header->command, header + 1, payload_length);
        input.Consume(total_size);
    }
}

inline void
TrafoConnection::OnPacket(TranslationCommand cmd, const void *payload, size_t length)
{
    assert(state != State::PROCESSING);

    if (cmd == TranslationCommand::BEGIN) {
        if (state != State::INIT) {
            daemon_log(2, "Misplaced INIT\n");
            listener.RemoveConnection(*this);
            return;
        }

        state = State::REQUEST;
    }

    if (state != State::REQUEST) {
        daemon_log(2, "INIT expected\n");
        listener.RemoveConnection(*this);
        return;
    }

    if (gcc_unlikely(cmd == TranslationCommand::END)) {
        state = State::PROCESSING;
        read_event.Delete();
        handler.OnTrafoRequest(*this, request);
        return;
    }

    request.Parse(cmd, payload, length);
}

void
TrafoConnection::TryWrite()
{
    assert(state == State::RESPONSE);

    ssize_t nbytes = send(fd.Get(), output.data, output.size,
                          MSG_DONTWAIT|MSG_NOSIGNAL);
    if (nbytes < 0) {
        if (gcc_likely(errno == EAGAIN)) {
            write_event.Add();
            return;
        }

        daemon_log(2, "Failed to write to client: %s\n", strerror(errno));
        listener.RemoveConnection(*this);
        return;
    }

    output.data += nbytes;
    output.size -= nbytes;

    if (output.empty()) {
        delete[] response;
        state = State::INIT;
        write_event.Delete();
        read_event.Add();
    }
}

void
TrafoConnection::SendResponse(TrafoResponse &&_response)
{
    assert(state == State::PROCESSING);

    state = State::RESPONSE;
    output = _response.Finish();
    response = output.data;

    TryWrite();
}
