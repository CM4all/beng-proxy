/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef UDP_HANDLER_HXX
#define UDP_HANDLER_HXX

#include <exception>

#include <stddef.h>

class SocketAddress;

class UdpHandler {
public:
    /**
     * @param uid the peer process uid, or -1 if unknown
     */
    virtual void OnUdpDatagram(const void *data, size_t length,
                               SocketAddress address, int uid) = 0;

    virtual void OnUdpError(std::exception_ptr ep) = 0;
};

#endif
