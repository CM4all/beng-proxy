/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_CONFIG_HXX
#define BENG_PROXY_SPAWN_CONFIG_HXX

#include "UidGid.hxx"

/**
 * Configuration for the spawner.
 */
struct SpawnConfig {
    UidGid default_uid_gid;

    void Init() {
        default_uid_gid.Init();
    }
};

#endif
