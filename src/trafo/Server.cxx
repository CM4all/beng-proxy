/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Server.hxx"

void
TrafoServer::ListenPath(const char *path)
{
    listeners.emplace_back(event_loop, handler);
    listeners.back().ListenPath(path);
}
