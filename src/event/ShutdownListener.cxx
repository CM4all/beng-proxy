/*
 * Listener for shutdown signals (SIGTERM, SIGINT, SIGQUIT).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ShutdownListener.hxx"

#include <inline/compiler.h>
#include <daemon/log.h>

#include <signal.h>
#include <unistd.h>

inline void
ShutdownListener::SignalCallback(int signo)
{
    daemon_log(2, "caught signal %d, shutting down (pid=%d)\n",
               signo, (int)getpid());

    Disable();
    callback();
}

ShutdownListener::ShutdownListener(EventLoop &loop, Callback _callback)
    :sigterm_event(loop, SIGTERM, BIND_THIS_METHOD(SignalCallback)),
     sigint_event(loop, SIGINT, BIND_THIS_METHOD(SignalCallback)),
     sigquit_event(loop, SIGQUIT, BIND_THIS_METHOD(SignalCallback)),
     callback(_callback)
{
}

void
ShutdownListener::Enable()
{
    sigterm_event.Add();
    sigint_event.Add();
    sigquit_event.Add();
}

void
ShutdownListener::Disable()
{
    sigterm_event.Delete();
    sigint_event.Delete();
    sigquit_event.Delete();
}

