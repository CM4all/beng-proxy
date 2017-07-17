/*
 * A simple server for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_SERVER_H
#define BENG_PROXY_LOG_SERVER_H

#include "Datagram.hxx"
#include "net/StaticSocketAddress.hxx"

#include <array>

/**
 * An extension of #AccessLogDatagram which contains information on
 * the receipt.
 */
struct ReceivedAccessLogDatagram : AccessLogDatagram {
    SocketAddress logger_client_address;
};

class AccessLogServer {
    const int fd;

    ReceivedAccessLogDatagram datagram;

    static constexpr size_t N = 32;
    std::array<StaticSocketAddress, N> addresses;
    std::array<uint8_t[16384], N> payloads;
    std::array<size_t, N> sizes;
    size_t n_payloads = 0, current_payload = 0;

public:
    explicit AccessLogServer(int _fd):fd(_fd) {}

    ~AccessLogServer();

    const ReceivedAccessLogDatagram *Receive();

    template<typename F>
    void Run(F &&f) {
        while (const auto *d = Receive())
            f(*d);
    }

private:
    bool Fill();
};

#endif
