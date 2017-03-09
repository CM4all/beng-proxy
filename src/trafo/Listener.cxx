/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Listener.hxx"
#include "net/SocketAddress.hxx"
#include "util/Exception.hxx"

#include <daemon/log.h>

void
TrafoListener::RemoveConnection(TrafoConnection &connection)
{
    connections.remove(connection);
}

void
TrafoListener::OnAccept(UniqueSocketDescriptor &&new_fd,
                        gcc_unused SocketAddress address)
{
    connections.emplace_back(event_loop, *this, handler, std::move(new_fd));
}

void
TrafoListener::OnAcceptError(std::exception_ptr ep)
{
    daemon_log(2, "%s\n", GetFullMessage(ep).c_str());
}
