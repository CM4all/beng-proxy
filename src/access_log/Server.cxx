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

#include "Server.hxx"
#include "Parser.hxx"

#include <sys/socket.h>
#include <stdlib.h>

AccessLogServer::AccessLogServer()
    :AccessLogServer(SocketDescriptor(STDIN_FILENO)) {}

bool
AccessLogServer::Fill()
{
    assert(current_payload >= n_payloads);

    std::array<struct iovec, N> iovs;
    std::array<struct mmsghdr, N> msgs;

    for (size_t i = 0; i < N; ++i) {
        auto &iov = iovs[i];
        iov.iov_base = payloads[i];
        iov.iov_len = sizeof(payloads[i]) - 1;

        auto &msg = msgs[i].msg_hdr;
        msg.msg_name = (struct sockaddr *)addresses[i];
        msg.msg_namelen = addresses[i].GetCapacity();
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
    }

    int n = recvmmsg(fd.Get(), &msgs.front(), msgs.size(),
                     MSG_WAITFORONE|MSG_CMSG_CLOEXEC, nullptr);
    if (n <= 0)
        return false;

    for (n_payloads = 0; n_payloads < size_t(n); ++n_payloads) {
        if (msgs[n_payloads].msg_len == 0)
            /* when the peer closes the socket, recvmmsg() doesn't
               return 0; instead, it fills the mmsghdr array with
               empty packets */
            break;

        if (msgs[n_payloads].msg_hdr.msg_namelen >= sizeof(struct sockaddr))
            addresses[n_payloads].SetSize(msgs[n_payloads].msg_hdr.msg_namelen);
        else
            addresses[n_payloads].Clear();

        sizes[n_payloads] = msgs[n_payloads].msg_len;
    }

    current_payload = 0;
    return n_payloads > 0;
}

const ReceivedAccessLogDatagram *
AccessLogServer::Receive()
{
    while (true) {
        if (current_payload >= n_payloads && !Fill())
            return nullptr;

        assert(current_payload < n_payloads);

        const SocketAddress address = addresses[current_payload];
        uint8_t *buffer = payloads[current_payload];
        size_t nbytes = sizes[current_payload];
        ++current_payload;

        /* force null termination so we can use string functions inside
           the buffer */
        buffer[nbytes] = 0;

        memset(&datagram, 0, sizeof(datagram));

        datagram.logger_client_address = address;
        datagram.raw = {buffer, nbytes};

        try {
            log_server_apply_datagram(&datagram, buffer, buffer + nbytes);
            return &datagram;
        } catch (AccessLogProtocolError) {
        }
    }
}

