/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_UID_GID_HXX
#define BENG_PROXY_UID_GID_HXX

#include <array>
#include <algorithm>

#include <sys/types.h>

struct UidGid {
    uid_t uid;
    gid_t gid;

    std::array<gid_t, 32> groups;

    constexpr UidGid():uid(0), gid(0), groups{{0}} {}

    /**
     * Look up a user name in the system user database (/etc/passwd)
     * and fill #uid, #gid and #groups.
     *
     * Throws std::runtime_error on error.
     */
    void Lookup(const char *username);

    void LoadEffective();

    constexpr bool IsEmpty() const {
        return uid == 0 && gid == 0 && !HasGroups();
    }

    constexpr bool IsComplete() const {
        return uid != 0 && gid != 0;
    }

    bool HasGroups() const {
        return groups.front() != 0;
    }

    size_t CountGroups() const {
        return std::distance(groups.begin(),
                             std::find(groups.begin(), groups.end(), 0));
    }

    char *MakeId(char *p) const;

    void Apply() const;
};

#endif
