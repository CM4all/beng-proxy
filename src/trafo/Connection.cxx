/*
 * Copyright 2007-2018 Content Management AG
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
#include "io/Logger.hxx"

#include <unistd.h>
#include <errno.h>
#include <string.h>

TrafoConnection::TrafoConnection(EventLoop &event_loop,
                                 TrafoListener &_listener,
                                 TrafoHandler &_handler,
                                 UniqueSocketDescriptor &&_fd)
    :listener(_listener), handler(_handler),
     fd(std::move(_fd)),
     event(event_loop, BIND_THIS_METHOD(OnSocketReady), fd),
     state(State::INIT),
     input(8192)
{
    event.ScheduleRead();
}

TrafoConnection::~TrafoConnection()
{
    if (state == State::RESPONSE)
        delete[] response;
}

inline bool
TrafoConnection::TryRead()
{
    assert(state == State::INIT || state == State::REQUEST);

    auto r = input.Write();
    assert(!r.empty());

    ssize_t nbytes = recv(fd.Get(), r.data, r.size, MSG_DONTWAIT);
    if (gcc_likely(nbytes > 0)) {
        input.Append(nbytes);
        return OnReceived();
    }

    if (nbytes < 0) {
        if (errno == EAGAIN)
            return true;

        LogConcat(2, "trafo", "Failed to read from client: ", strerror(errno));
    }

    listener.RemoveConnection(*this);
    return false;
}

inline bool
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

        if (!OnPacket(header->command, header + 1, payload_length))
            return false;

        input.Consume(total_size);
    }

    return true;
}

inline bool
TrafoConnection::OnPacket(TranslationCommand cmd, const void *payload, size_t length)
{
    assert(state != State::PROCESSING);

    if (cmd == TranslationCommand::BEGIN) {
        if (state != State::INIT) {
            LogConcat(2, "trafo", "Misplaced INIT");
            listener.RemoveConnection(*this);
            return false;
        }

        state = State::REQUEST;
    }

    if (state != State::REQUEST) {
        LogConcat(2, "trafo", "INIT expected");
        listener.RemoveConnection(*this);
        return false;
    }

    if (gcc_unlikely(cmd == TranslationCommand::END)) {
        state = State::PROCESSING;
        event.CancelRead();
        handler.OnTrafoRequest(*this, request);
        return false;
    }

    request.Parse(cmd, payload, length);
    return true;
}

void
TrafoConnection::TryWrite()
{
    assert(state == State::RESPONSE);

    ssize_t nbytes = send(fd.Get(), output.data, output.size,
                          MSG_DONTWAIT|MSG_NOSIGNAL);
    if (nbytes < 0) {
        if (gcc_likely(errno == EAGAIN)) {
            event.ScheduleWrite();
            return;
        }

        LogConcat(2, "trafo", "Failed to write to client: ", strerror(errno));
        listener.RemoveConnection(*this);
        return;
    }

    output.data += nbytes;
    output.size -= nbytes;

    if (output.empty()) {
        delete[] response;
        state = State::INIT;
        event.Schedule(SocketEvent::READ);
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

void
TrafoConnection::OnSocketReady(unsigned events) noexcept
{
    if (events & SocketEvent::READ) {
        if (!TryRead())
            return;
    }

    if (events & SocketEvent::WRITE)
        TryWrite();
}
