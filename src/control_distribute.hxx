/*
 * control_handler wrapper which publishes raw packets to
 * #UdpDistribute.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_DISTRIBUTE_HXX
#define BENG_PROXY_CONTROL_DISTRIBUTE_HXX

#include "beng-proxy/control.h"
#include "glibfwd.hxx"

#include <stddef.h>

struct ControlServer;
struct control_handler;
struct UdpDistribute;
class SocketAddress;

class ControlDistribute {
    UdpDistribute *const distribute;

    const struct control_handler &next_handler;
    void *next_ctx;

public:
    ControlDistribute(const struct control_handler &_next_handler,
                      void *_next_ctx);
    ~ControlDistribute();

    int Add();
    void Clear();

    static const struct control_handler handler;

private:
    static bool OnControlRaw(const void *data, size_t length,
                             SocketAddress address,
                             int uid,
                             void *ctx);

    static void OnControlPacket(ControlServer &control_server,
                                enum beng_control_command command,
                                const void *payload, size_t payload_length,
                                SocketAddress address,
                                void *ctx);

    static void OnControlError(GError *error, void *ctx);
};

#endif
