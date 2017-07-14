/*
 * Server side part of the "control" protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef CONTROL_HANDLER_HXX
#define CONTROL_HANDLER_HXX

#include "beng-proxy/control.h"

#include <exception>

#include <stddef.h>

class SocketAddress;
class ControlServer;

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

    virtual void OnControlError(std::exception_ptr ep) = 0;
};

#endif
