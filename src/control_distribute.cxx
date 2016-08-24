/*
 * control_handler wrapper which publishes raw packets to
 * #UdpDistribute.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control_distribute.hxx"
#include "control_handler.hxx"
#include "net/SocketAddress.hxx"
#include "net/UdpDistribute.hxx"

#include <utility>

ControlDistribute::ControlDistribute(ControlHandler &_next_handler)
    :distribute(udp_distribute_new()),
     next_handler(_next_handler)
{
}

ControlDistribute::~ControlDistribute()
{
    udp_distribute_free(distribute);
}

int
ControlDistribute::Add()
{
    return udp_distribute_add(distribute);
}

void
ControlDistribute::Clear()
{
    udp_distribute_clear(distribute);
}

bool
ControlDistribute::OnControlRaw(const void *data, size_t length,
                                SocketAddress address, int uid)
{
    /* forward the packet to all worker processes */
    udp_distribute_packet(distribute, data, length);

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
ControlDistribute::OnControlError(Error &&error)
{
    return next_handler.OnControlError(std::move(error));
}
