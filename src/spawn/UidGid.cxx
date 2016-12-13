/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "UidGid.hxx"
#include "system/Error.hxx"
#include "util/RuntimeError.hxx"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>

void
UidGid::Lookup(const char *username)
{
    errno = 0;
    const auto *pw = getpwnam(username);
    if (pw == nullptr) {
        if (errno == 0 || errno == ENOENT)
            throw FormatRuntimeError("No such user: %s", username);
        else
            throw FormatErrno("Failed to look up user '%s'", username);
    }

    uid = pw->pw_uid;
    gid = pw->pw_gid;

    int ngroups = groups.size();
    int n = getgrouplist(username, pw->pw_gid, &groups.front(), &ngroups);
    if (n >= 0 && (size_t)n < groups.size())
        groups[n] = 0;
}

void
UidGid::LoadEffective()
{
    uid = geteuid();
    gid = getegid();
}

char *
UidGid::MakeId(char *p) const
{
    if (uid != 0)
        p += sprintf(p, ";uid%d", int(uid));

    if (gid != 0)
        p += sprintf(p, ";gid%d", int(gid));

    return p;
}

void
UidGid::Apply() const
{
    if (gid != 0 && setregid(gid, gid) < 0) {
        fprintf(stderr, "failed to setgid %d: %s\n",
                int(gid), strerror(errno));
        _exit(EXIT_FAILURE);
    }

    if (HasGroups()) {
        if (setgroups(CountGroups(), &groups.front()) < 0) {
            fprintf(stderr, "setgroups() failed: %s\n", strerror(errno));
            _exit(EXIT_FAILURE);
        }
    } else if (gid != 0) {
        if (setgroups(0, &gid) < 0) {
            fprintf(stderr, "setgroups(%d) failed: %s\n",
                    int(gid), strerror(errno));
            _exit(EXIT_FAILURE);
        }
    }

    if (uid != 0 && setreuid(uid, uid) < 0) {
        fprintf(stderr, "failed to setuid %d: %s\n",
                int(uid), strerror(errno));
        _exit(EXIT_FAILURE);
    }
}
