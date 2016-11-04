/*
 * control_handler wrapper which publishes raw packets to
 * #UdpDistribute.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control_distribute.hxx"
#include "control_handler.hxx"
#include "net/SocketAddress.hxx"

ControlDistribute::ControlDistribute(EventLoop &event_loop,
                                     ControlHandler &_next_handler)
    :distribute(event_loop),
     next_handler(_next_handler)
{
}

bool
ControlDistribute::OnControlRaw(const void *data, size_t length,
                                SocketAddress address, int uid)
{
    /* forward the packet to all worker processes */
    distribute.Packet(data, length);

    return next_handler.OnControlRaw(data, length, address, uid);
}

void
ControlDistribute::OnControlPacket(ControlServer &control_server,
                                   enum beng_control_command command,
                                   const void *payload, size_t payload_length,
                                   SocketAddress address)
{
    return next_handler.OnControlPacket(control_server, command,
                                        payload, payload_length, address);
}

void
ControlDistribute::OnControlError(std::exception_ptr ep)
{
    return next_handler.OnControlError(ep);
}
