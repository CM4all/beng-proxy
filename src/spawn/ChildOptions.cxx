/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ChildOptions.hxx"
#include "ResourceLimits.hxx"
#include "JailParams.hxx"
#include "Prepared.hxx"
#include "AllocatorPtr.hxx"
#include "pexpand.hxx"
#include "gerrno.h"
#include "util/djbhash.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

ChildOptions::ChildOptions(AllocatorPtr alloc,
                           const ChildOptions &src)
    :stderr_path(alloc.CheckDup(src.stderr_path)),
     expand_stderr_path(alloc.CheckDup(src.expand_stderr_path)),
     env(alloc, src.env),
     cgroup(alloc, src.cgroup),
     rlimits(src.rlimits != nullptr
             ? alloc.New<ResourceLimits>(*src.rlimits)
             : nullptr),
     refence(alloc, src.refence),
     ns(alloc, src.ns),
     jail(src.jail != nullptr
          ? alloc.New<JailParams>(*src.jail)
          : nullptr),
     uid_gid(src.uid_gid),
     no_new_privs(src.no_new_privs)
{
}

bool
ChildOptions::Check(GError **error_r) const
{
    return jail == nullptr || jail->Check(error_r);
}

bool
ChildOptions::IsExpandable() const
{
    return expand_stderr_path != nullptr ||
        env.IsExpandable() ||
        ns.IsExpandable() ||
        (jail != nullptr && jail->IsExpandable());
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
        (jail == nullptr || jail->Expand(pool, match_info, error_r));
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
    if (rlimits != nullptr)
        p = rlimits->MakeId(p);
    p = refence.MakeId(p);
    p = ns.MakeId(p);
    if (jail != nullptr)
        p = jail->MakeId(p);
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
    if (use_jail && jail != nullptr)
        jail->InsertWrapper(dest, document_root);

    if (stderr_path != nullptr) {
        int fd = OpenStderrPath();
        if (fd < 0) {
            int code = errno;
            g_set_error(error_r, errno_quark(), errno, "open('%s') failed: %s",
                        stderr_path, strerror(code));
            return false;
        }

        dest.SetStderr(fd);
    }

    for (const char *e : env)
        dest.PutEnv(e);

    dest.cgroup = cgroup;
    dest.refence = refence;
    dest.ns = ns;
    if (rlimits != nullptr)
        dest.rlimits = *rlimits;
    dest.uid_gid = uid_gid;
    dest.no_new_privs = no_new_privs;

    return true;
}
