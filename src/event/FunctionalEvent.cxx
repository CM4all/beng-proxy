/*
 * C++ wrappers for libevent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "FunctionalEvent.hxx"

void
FunctionalEvent::Callback(int fd, short mask, void *ctx)
{
    FunctionalEvent &event = *(FunctionalEvent *)ctx;

    event.handler(fd, mask);
}
