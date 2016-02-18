/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_EXIT_LISTENER_HXX
#define BENG_PROXY_SPAWN_EXIT_LISTENER_HXX

/**
 * This interface gets notified when the registered child process
 * exits.
 */
class ExitListener {
public:
    virtual void OnChildProcessExit(int status) = 0;
};

#endif
