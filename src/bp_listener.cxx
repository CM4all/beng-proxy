/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_listener.hxx"
#include "bp_connection.hxx"
#include "util/Error.hxx"
#include "net/SocketAddress.hxx"

#include <daemon/log.h>

void
BPListener::OnAccept(SocketDescriptor &&fd, SocketAddress address)
{
    new_connection(&instance, std::move(fd), address);
}

void
BPListener::OnAcceptError(Error &&error)
{
    daemon_log(2, "%s\n", error.GetMessage());
}
