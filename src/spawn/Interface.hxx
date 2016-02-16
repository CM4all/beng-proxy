/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_INTERFACE_HXX
#define BENG_PROXY_SPAWN_INTERFACE_HXX

#include "glibfwd.hxx"

struct PreparedChildProcess;
class ExitListener;

/**
 * A service which can spawn new child processes according to a
 * #PreparedChildProcess instance.
 */
class SpawnService {
public:
    /**
     * @return a process id or -1 on error
     */
    virtual int SpawnChildProcess(const char *name,
                                  PreparedChildProcess &&params,
                                  ExitListener *listener,
                                  GError **error_r) = 0;

    /**
     * Send a signal to a child process and unregister it.
     */
    virtual void KillChildProcess(int pid, int signo) = 0;

    /**
     * Send SIGTERM to the given child process.
     */
    void KillChildProcess(int pid);
};

#endif
