/*
 * Listener for shutdown signals (SIGTERM, SIGINT, SIGQUIT).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SHUTDOWN_LISTENER_HXX
#define BENG_PROXY_SHUTDOWN_LISTENER_HXX

#include "SignalEvent.hxx"

class ShutdownListener {
    SignalEvent event;

    typedef BoundMethod<> Callback;
    const Callback callback;

public:
    ShutdownListener(EventLoop &loop, Callback _callback);

    ~ShutdownListener() {
        Disable();
    }

    ShutdownListener(const ShutdownListener &) = delete;
    ShutdownListener &operator=(const ShutdownListener &) = delete;

    void Enable() {
        event.Enable();
    }

    void Disable() {
        event.Disable();
    }

private:
    void SignalCallback(int signo);
};

#endif
