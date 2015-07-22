/*
 * C++ wrappers for libevent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "SignalEvent.hxx"

#include <inline/compiler.h>

void
SignalEvent::Callback(gcc_unused int fd, gcc_unused short mask, void *ctx)
{
    SignalEvent &event = *(SignalEvent *)ctx;

    event.handler();
}
