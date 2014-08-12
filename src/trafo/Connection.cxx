/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Connection.hxx"
#include "Listener.hxx"
#include "Handler.hxx"
#include "Response.hxx"

#include <beng-proxy/translation.h>

#include <daemon/log.h>

#include <unistd.h>
#include <errno.h>
#include <string.h>

TrafoConnection::TrafoConnection(TrafoListener &_listener,
                                 TrafoHandler &_handler,
                                 SocketDescriptor &&_fd)
    :listener(_listener), handler(_handler),
     fd(std::move(_fd)),
     read_event([this](int, short){ TryRead(); }),
     write_event([this](int, short){ TryWrite(); }),
     state(State::INIT),
     input(8192)
{
    read_event.SetAdd(fd.Get(), EV_READ|EV_PERSIST);
    write_event.Set(fd.Get(), EV_WRITE|EV_PERSIST);
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
    assert(!r.IsEmpty());

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
        const beng_translation_header *header =
            (const beng_translation_header *)p;
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
TrafoConnection::OnPacket(unsigned _cmd, const void *payload, size_t length)
{
    assert(state != State::PROCESSING);

    const beng_translation_command cmd = beng_translation_command(_cmd);
    if (cmd == TRANSLATE_BEGIN) {
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

    if (gcc_unlikely(cmd == TRANSLATE_END)) {
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

    if (output.IsEmpty()) {
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
