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

struct pool;
struct in_addr;
class SocketAddress;
class Error;
class UdpListener;

struct ControlServer final : UdpHandler {
    UdpListener *udp = nullptr;

    ControlHandler &handler;

    explicit ControlServer(ControlHandler &_handler)
        :handler(_handler) {}

    ~ControlServer();

    void Open(SocketAddress address,
              const struct in_addr *group=nullptr);

    void OpenPort(const char *host_and_port, int default_port,
                  const struct in_addr *group);

    void Enable();
    void Disable();

    /**
     * Replaces the socket.  The old one is closed, and the new one is
     * now owned by this object.
     */
    void SetFd(int fd);

    bool Reply(struct pool *pool,
               SocketAddress address,
               enum beng_control_command command,
               const void *payload, size_t payload_length,
               Error &error_r);

    /* virtual methods from class UdpHandler */
    void OnUdpDatagram(const void *data, size_t length,
                       SocketAddress address, int uid) override;
    void OnUdpError(Error &&error) override;
};

#endif
