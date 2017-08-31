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

/**
 * A simple server for the logging protocol.
 */
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

    /**
     * Construct an instance with the default socket (STDIN_FILENO).
     */
    AccessLogServer();

    AccessLogServer(const AccessLogServer &) = delete;
    AccessLogServer &operator=(const AccessLogServer &) = delete;

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
