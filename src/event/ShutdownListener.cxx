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
    callback(callback_ctx);
}

ShutdownListener::ShutdownListener(EventLoop &loop,
                                   void (*_callback)(void *ctx), void *_ctx)
    :sigterm_event(loop, SIGTERM, BIND_THIS_METHOD(SignalCallback)),
     sigint_event(loop, SIGINT, BIND_THIS_METHOD(SignalCallback)),
     sigquit_event(loop, SIGQUIT, BIND_THIS_METHOD(SignalCallback)),
     callback(_callback), callback_ctx(_ctx)
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

