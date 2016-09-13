/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SPAWN_LOCAL_HXX
#define SPAWN_LOCAL_HXX

#include "Interface.hxx"
#include "Config.hxx"

class ChildProcessRegistry;

class LocalSpawnService final : public SpawnService {
    const SpawnConfig &config;

    ChildProcessRegistry &registry;

public:
    explicit LocalSpawnService(const SpawnConfig &_config,
                               ChildProcessRegistry &_registry)
        :config(_config), registry(_registry) {}

    int SpawnChildProcess(const char *name, PreparedChildProcess &&params,
                          ExitListener *listener) override;

    void SetExitListener(int pid, ExitListener *listener) override;

    void KillChildProcess(int pid, int signo) override;
};

#endif
