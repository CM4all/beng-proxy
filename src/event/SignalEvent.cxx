/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "SignalEvent.hxx"

void
SignalEvent::SignalCallback(evutil_socket_t fd,
                            gcc_unused short events,
                            void *ctx)
{
    auto &event = *(SignalEvent *)ctx;
    event.callback(fd);
}
