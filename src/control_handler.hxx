/*
 * Server side part of the "control" protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef CONTROL_HANDLER_HXX
#define CONTROL_HANDLER_HXX

#include "beng-proxy/control.h"

#include <stddef.h>

class Error;
class SocketAddress;
struct ControlServer;

class ControlHandler {
public:
    /**
     * @return false if the datagram shall be discarded
     */
    virtual bool OnControlRaw(const void *data, size_t length,
                              SocketAddress address,
                              int uid);

    virtual void OnControlPacket(ControlServer &control_server,
                                 enum beng_control_command command,
                                 const void *payload, size_t payload_length,
                                 SocketAddress address) = 0;

    virtual void OnControlError(Error &&error) = 0;
};

#endif
