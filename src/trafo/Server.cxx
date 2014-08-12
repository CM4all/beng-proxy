/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Server.hxx"

bool
TrafoServer::ListenPath(const char *path, Error &error)
{
    listeners.emplace_back(handler);

    if (!listeners.back().ListenPath(path, error)) {
        listeners.pop_back();
        return false;
    }

    return true;
}
