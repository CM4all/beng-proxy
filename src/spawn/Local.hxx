/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SPAWN_LOCAL_HXX
#define SPAWN_LOCAL_HXX

#include "Interface.hxx"

class ChildProcessRegistry;

class LocalSpawnService final : public SpawnService {
    ChildProcessRegistry &registry;
public:
    explicit LocalSpawnService(ChildProcessRegistry &_registry)
        :registry(_registry) {}

    int SpawnChildProcess(const char *name, PreparedChildProcess &&params,
                          ExitListener *listener,
                          GError **error_r) override;

    void SetExitListener(int pid, ExitListener *listener) override;

    void KillChildProcess(int pid, int signo) override;
};

#endif
