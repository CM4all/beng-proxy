/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_CONFIG_HXX
#define BENG_PROXY_SPAWN_CONFIG_HXX

#include "UidGid.hxx"

#include <inline/compiler.h>

#include <set>

/**
 * Configuration for the spawner.
 */
struct SpawnConfig {
    UidGid default_uid_gid;

    std::set<uid_t> allowed_uids;
    std::set<gid_t> allowed_gids;

    /**
     * Ignore the user namespaces setting?  This is used as a
     * workaround to allow the spawner run as root.
     *
     * TODO: replace this workaround
     */
    bool ignore_userns;

    void Init() {
        default_uid_gid.Init();
        ignore_userns = false;
    }

    gcc_pure
    bool VerifyUid(uid_t uid) const {
        return allowed_uids.find(uid) != allowed_uids.end();
    }

    gcc_pure
    bool VerifyGid(gid_t gid) const {
        return allowed_gids.find(gid) != allowed_gids.end();
    }

    gcc_pure
    bool Verify(const UidGid &uid_gid) const {
        return VerifyUid(uid_gid.uid) && VerifyGid(uid_gid.gid);
    }
};

#endif
