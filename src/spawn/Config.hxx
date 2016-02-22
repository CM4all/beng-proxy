/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_CONFIG_HXX
#define BENG_PROXY_SPAWN_CONFIG_HXX

#include "UidGid.hxx"

#include <inline/compiler.h>

/**
 * Configuration for the spawner.
 */
struct SpawnConfig {
    UidGid default_uid_gid;

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
        // TODO: replace hard-coded list
        return uid == 33 || uid == 33333;
    }

    gcc_pure
    bool VerifyGid(gid_t gid) const {
        // TODO: replace hard-coded list
        return gid == 33 || gid == 33333;
    }

    gcc_pure
    bool Verify(const UidGid &uid_gid) const {
        return VerifyUid(uid_gid.uid) && VerifyGid(uid_gid.gid);
    }
};

#endif
