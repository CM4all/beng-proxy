/*
 * Listener for shutdown signals (SIGTERM, SIGINT, SIGQUIT).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SHUTDOWN_LISTENER_HXX
#define BENG_PROXY_SHUTDOWN_LISTENER_HXX

#include "SignalEvent.hxx"

class ShutdownListener {
    SignalEvent sigterm_event, sigint_event, sigquit_event;

    typedef BoundMethod<> Callback;
    const Callback callback;

public:
    ShutdownListener(EventLoop &loop, Callback _callback);

    ShutdownListener(const ShutdownListener &) = delete;
    ShutdownListener &operator=(const ShutdownListener &) = delete;

    void Enable();
    void Disable();

private:
    void SignalCallback(int signo);
};

#endif
