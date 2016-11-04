/*
 * control_handler wrapper which publishes raw packets to
 * #UdpDistribute.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_DISTRIBUTE_HXX
#define BENG_PROXY_CONTROL_DISTRIBUTE_HXX

#include "control_handler.hxx"
#include "net/UdpDistribute.hxx"

#include <stddef.h>

struct ControlServer;
class EventLoop;
class SocketAddress;

class ControlDistribute final : public ControlHandler {
    UdpDistribute distribute;

    ControlHandler &next_handler;

public:
    ControlDistribute(EventLoop &event_loop, ControlHandler &_next_handler);

    int Add() {
        return distribute.Add();
    }

    void Clear() {
        distribute.Clear();
    }

    static const struct control_handler handler;

private:
    /* virtual methods from class ControlHandler */
    bool OnControlRaw(const void *data, size_t length,
                      SocketAddress address, int uid) override;

    void OnControlPacket(ControlServer &control_server,
                         enum beng_control_command command,
                         const void *payload, size_t payload_length,
                         SocketAddress address) override;

    void OnControlError(std::exception_ptr ep) override;
};

#endif
