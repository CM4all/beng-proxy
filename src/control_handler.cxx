/*
 * Server side part of the "control" protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control_handler.hxx"
#include "net/SocketAddress.hxx"

bool
ControlHandler::OnControlRaw(gcc_unused const void *data,
                             gcc_unused size_t length,
                             gcc_unused SocketAddress address,
                             gcc_unused int uid)
{
    return true;
}
