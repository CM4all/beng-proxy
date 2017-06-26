/*
 * A simple server for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_SERVER_H
#define BENG_PROXY_LOG_SERVER_H

#include "Datagram.hxx"

class AccessLogServer {
    const int fd;

    AccessLogDatagram datagram;

    char buffer[65536];

public:
    explicit AccessLogServer(int _fd):fd(_fd) {}

    ~AccessLogServer();

    const AccessLogDatagram *Receive();

    template<typename F>
    void Run(F &&f) {
        while (const auto *d = Receive())
            f(*d);
    }
};

#endif
