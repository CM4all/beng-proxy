/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ChildOptions.hxx"
#include "Prepared.hxx"
#include "pool.hxx"
#include "pexpand.hxx"
#include "gerrno.h"
#include "util/djbhash.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

ChildOptions::ChildOptions(struct pool *pool,
                           const ChildOptions &src)
    :stderr_path(p_strdup_checked(pool, src.stderr_path)),
     expand_stderr_path(p_strdup_checked(pool, src.expand_stderr_path)),
     env(*pool, src.env),
     cgroup(*pool, src.cgroup),
     rlimits(src.rlimits),
     refence(*pool, src.refence),
     ns(pool, src.ns),
     jail(pool, src.jail),
     uid_gid(src.uid_gid),
     no_new_privs(src.no_new_privs)
{
}

bool
ChildOptions::Expand(struct pool &pool, const MatchInfo &match_info,
                     Error &error_r)
{
    if (expand_stderr_path != nullptr) {
        stderr_path = expand_string_unescaped(&pool, expand_stderr_path,
                                              match_info, error_r);
        if (stderr_path == nullptr)
            return false;
    }

    return env.Expand(&pool, match_info, error_r) &&
        ns.Expand(pool, match_info, error_r) &&
        jail.Expand(pool, match_info, error_r);
}

char *
ChildOptions::MakeId(char *p) const
{
    if (stderr_path != nullptr)
        p += sprintf(p, ";e%08x", djb_hash_string(stderr_path));

    for (auto i : env) {
        *p++ = '$';
        p = stpcpy(p, i);
    }

    p = cgroup.MakeId(p);
    p = rlimits.MakeId(p);
    p = refence.MakeId(p);
    p = ns.MakeId(p);
    p = jail.MakeId(p);
    p = uid_gid.MakeId(p);

    if (no_new_privs) {
        *p++ = ';';
        *p++ = 'n';
    }

    return p;
}

int
ChildOptions::OpenStderrPath() const
{
    assert(stderr_path != nullptr);

    return open(stderr_path, O_CREAT|O_WRONLY|O_APPEND|O_CLOEXEC|O_NOCTTY,
                0666);
}

bool
ChildOptions::CopyTo(PreparedChildProcess &dest,
                     bool use_jail,
                     const char *document_root,
                     GError **error_r) const
{
    if (use_jail)
        jail.InsertWrapper(dest, document_root);

    if (stderr_path != nullptr) {
        dest.SetStderr(OpenStderrPath());
        if (dest.stderr_fd < 0) {
            int code = errno;
            g_set_error(error_r, errno_quark(), errno, "open('%s') failed: %s",
                        stderr_path, strerror(code));
            return false;
        }
    }

    for (const char *e : env)
        dest.PutEnv(e);

    dest.cgroup = cgroup;
    dest.refence = refence;
    dest.ns = ns;
    dest.rlimits = rlimits;
    dest.uid_gid = uid_gid;
    dest.no_new_privs = no_new_privs;

    return true;
}
