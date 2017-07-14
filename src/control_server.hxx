/*
 * Server side part of the "control" protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_SERVER_H
#define BENG_PROXY_CONTROL_SERVER_H

#include "beng-proxy/control.h"
#include "control_handler.hxx"
#include "net/UdpHandler.hxx"

#include <stddef.h>

class SocketAddress;
class UniqueSocketDescriptor;
class Error;
class UdpListener;
class EventLoop;
struct UdpListenerConfig;

class ControlServer final : UdpHandler {
    ControlHandler &handler;

    UdpListener *const udp;

public:
    ControlServer(EventLoop &event_loop, ControlHandler &_handler,
                  const UdpListenerConfig &config);

    ~ControlServer();

    void Enable();
    void Disable();

    /**
     * Replaces the socket.  The old one is closed, and the new one is
     * now owned by this object.
     */
    void SetFd(UniqueSocketDescriptor &&fd);

    /**
     * Throws std::runtime_error on error.
     */
    void Reply(SocketAddress address,
               enum beng_control_command command,
               const void *payload, size_t payload_length);

private:
    /* virtual methods from class UdpHandler */
    void OnUdpDatagram(const void *data, size_t length,
                       SocketAddress address, int uid) override;
    void OnUdpError(std::exception_ptr ep) override;
};

#endif
