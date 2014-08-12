/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ADDRESS_ENVELOPE_HXX
#define BENG_PROXY_ADDRESS_ENVELOPE_HXX

#include "net/SocketAddress.hxx"

#include <sys/socket.h>

struct address_envelope {
    socklen_t length;

    struct sockaddr address;

    operator SocketAddress() const {
        return { &address, length };
    }
};

#endif
