/*
 * Send notifications from a worker thread to the main thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "notify.hxx"
#include "system/Error.hxx"
#include "event/Callback.hxx"

#include <unistd.h>
#include <sys/eventfd.h>

static int
MakeEventFd()
{
    int fd = eventfd(0, EFD_NONBLOCK|EFD_CLOEXEC);
    if (fd < 0)
        throw MakeErrno("eventfd() failed");
    return fd;
}

Notify::Notify(Callback _callback)
    :callback(_callback),
     fd(MakeEventFd()),
     event(fd, EV_READ|EV_PERSIST,
           MakeSimpleEventCallback(Notify, EventFdCallback),
           this),
     pending(false) {
    event.Add();
}

Notify::~Notify()
{
    event.Delete();
    close(fd);
}

inline void
Notify::EventFdCallback()
{
    uint64_t value;
    (void)read(fd, &value, sizeof(value));

    if (pending.exchange(false))
        callback();
}
