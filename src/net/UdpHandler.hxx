/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef UDP_HANDLER_HXX
#define UDP_HANDLER_HXX

#include <stddef.h>

class SocketAddress;
class Error;

class UdpHandler {
public:
    /**
     * @param uid the peer process uid, or -1 if unknown
     */
    virtual void OnUdpDatagram(const void *data, size_t length,
                               SocketAddress address, int uid) = 0;

    virtual void OnUdpError(Error &&error) = 0;
};

#endif
