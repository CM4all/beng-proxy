/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "SignalEvent.hxx"
#include "system/Error.hxx"

#include <sys/signalfd.h>
#include <unistd.h>

SignalEvent::SignalEvent(EventLoop &loop, Callback _callback)
    :event(loop, BIND_THIS_METHOD(EventCallback)), callback(_callback)
{
    sigemptyset(&mask);
}

SignalEvent::~SignalEvent()
{
    if (fd >= 0)
        close(fd);
}

void
SignalEvent::Enable()
{
    fd = signalfd(fd, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
    if (fd < 0)
        throw MakeErrno("signalfd() failed");

    event.Set(fd, EV_READ|EV_PERSIST);
    event.Add();

    sigprocmask(SIG_BLOCK, &mask, nullptr);
}

void
SignalEvent::Disable()
{
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);

    event.Delete();
}

void
SignalEvent::EventCallback(unsigned)
{
    struct signalfd_siginfo info;
    ssize_t nbytes = read(fd, &info, sizeof(info));
    if (nbytes <= 0) {
        // TODO: log error?
        Disable();
        return;
    }

    callback(info.ssi_signo);
}
