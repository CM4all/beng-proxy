/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_UID_GID_HXX
#define BENG_PROXY_UID_GID_HXX

#include <sys/types.h>

struct UidGid {
    uid_t uid;
    gid_t gid;

    void Init() {
        uid = 0;
        gid = 0;
    }

    void LoadEffective();

    constexpr bool IsEmpty() const {
        return uid == 0 && gid == 0;
    }

    constexpr bool IsComplete() const {
        return uid != 0 && gid != 0;
    }

    char *MakeId(char *p) const;

    void Apply() const;
};

#endif
