/*
 * control_handler wrapper which publishes raw packets to
 * #UdpDistribute.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control_distribute.hxx"
#include "control_handler.hxx"
#include "net/SocketAddress.hxx"
#include "udp_distribute.hxx"

const struct control_handler ControlDistribute::handler = {
    .raw = OnControlRaw,
    .packet = OnControlPacket,
    .error = OnControlError,
};

ControlDistribute::ControlDistribute(const struct control_handler &_next_handler,
                                     void *_next_ctx)
    :distribute(udp_distribute_new()),
     next_handler(_next_handler), next_ctx(_next_ctx)
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
                                SocketAddress address, int uid,
                                void *ctx)
{
    ControlDistribute &d = *(ControlDistribute *)ctx;

    /* forward the packet to all worker processes */
    udp_distribute_packet(d.distribute, data, length);

    return d.next_handler.raw == nullptr ||
        d.next_handler.raw(data, length, address, uid, d.next_ctx);
}

void
ControlDistribute::OnControlPacket(ControlServer &control_server,
                                   enum beng_control_command command,
                                   const void *payload, size_t payload_length,
                                   SocketAddress address,
                                   void *ctx)
{
    ControlDistribute &d = *(ControlDistribute *)ctx;

    return d.next_handler.packet(control_server, command,
                                 payload, payload_length, address,
                                 d.next_ctx);
}

void
ControlDistribute::OnControlError(GError *error, void *ctx)
{
    ControlDistribute &d = *(ControlDistribute *)ctx;

    return d.next_handler.error(error, d.next_ctx);
}
