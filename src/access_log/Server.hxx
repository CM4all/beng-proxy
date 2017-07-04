/*
 * A simple server for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_SERVER_H
#define BENG_PROXY_LOG_SERVER_H

#include "Datagram.hxx"

#include <array>

class AccessLogServer {
    const int fd;

    AccessLogDatagram datagram;

    static constexpr size_t N = 32;
    std::array<uint8_t[16384], N> payloads;
    std::array<size_t, N> sizes;
    size_t n_payloads = 0, current_payload = 0;

public:
    explicit AccessLogServer(int _fd):fd(_fd) {}

    ~AccessLogServer();

    const AccessLogDatagram *Receive();

    template<typename F>
    void Run(F &&f) {
        while (const auto *d = Receive())
            f(*d);
    }

private:
    bool Fill();
};

#endif
