/*
 * Server side part of the "control" protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef CONTROL_HANDLER_HXX
#define CONTROL_HANDLER_HXX

#include "glibfwd.hxx"

#include <stddef.h>

class SocketAddress;
struct ControlServer;

struct control_handler {
    /**
     * @return false if the datagram shall be discarded
     */
    bool (*raw)(const void *data, size_t length,
                SocketAddress address,
                int uid,
                void *ctx);

    void (*packet)(ControlServer &control_server,
                   enum beng_control_command command,
                   const void *payload, size_t payload_length,
                   SocketAddress address,
                   void *ctx);

    void (*error)(GError *error, void *ctx);
};

#endif
