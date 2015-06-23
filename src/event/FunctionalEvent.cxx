/*
 * C++ wrappers for libevent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "FunctionalEvent.hxx"

#include <inline/compiler.h>

void
FunctionalEvent::Callback(int fd, short mask, void *ctx)
{
    FunctionalEvent &event = *(FunctionalEvent *)ctx;

    event.handler(fd, mask);
}

void
SignalEvent::Callback(gcc_unused int fd, gcc_unused short mask, void *ctx)
{
    SignalEvent &event = *(SignalEvent *)ctx;

    event.handler();
}
