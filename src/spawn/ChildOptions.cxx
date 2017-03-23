/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ChildOptions.hxx"
#include "ResourceLimits.hxx"
#include "Prepared.hxx"
#include "AllocatorPtr.hxx"
#include "system/Error.hxx"
#include "util/djbhash.h"

#if TRANSLATION_ENABLE_JAILCGI
#include "JailParams.hxx"
#endif

#if TRANSLATION_ENABLE_EXPAND
#include "pexpand.hxx"
#endif

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
#if TRANSLATION_ENABLE_JAILCGI
     jail(src.jail != nullptr
          ? alloc.New<JailParams>(*src.jail)
          : nullptr),
#endif
     uid_gid(src.uid_gid),
     stderr_null(src.stderr_null),
     no_new_privs(src.no_new_privs)
{
}

void
ChildOptions::Check() const
{
#if TRANSLATION_ENABLE_JAILCGI
    if (jail != nullptr)
        jail->Check();
#endif
}

#if TRANSLATION_ENABLE_EXPAND

bool
ChildOptions::IsExpandable() const
{
    return expand_stderr_path != nullptr ||
        env.IsExpandable() ||
        ns.IsExpandable() ||
        (jail != nullptr && jail->IsExpandable());
}

void
ChildOptions::Expand(AllocatorPtr alloc, const MatchInfo &match_info)
{
    if (expand_stderr_path != nullptr)
        stderr_path = expand_string_unescaped(alloc, expand_stderr_path,
                                              match_info);

    env.Expand(alloc, match_info);
    ns.Expand(alloc, match_info);

    if (jail != nullptr)
        jail->Expand(alloc, match_info);
}

#endif

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
#if TRANSLATION_ENABLE_JAILCGI
    if (jail != nullptr)
        p = jail->MakeId(p);
#endif
    p = uid_gid.MakeId(p);

    if (stderr_null) {
        *p++ = ';';
        *p++ = 'e';
        *p++ = 'n';
    }

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

void
ChildOptions::CopyTo(PreparedChildProcess &dest
#if TRANSLATION_ENABLE_JAILCGI
                     , bool use_jail, const char *document_root
#endif
                     ) const
{
#if TRANSLATION_ENABLE_JAILCGI
    if (use_jail && jail != nullptr)
        jail->InsertWrapper(dest, document_root);
#endif

    if (stderr_path != nullptr) {
        int fd = OpenStderrPath();
        if (fd < 0)
            throw FormatErrno("open('%s') failed", stderr_path);

        dest.SetStderr(fd);
    } else if (stderr_null) {
        const char *path = "/dev/null";
        int fd = open(path, O_WRONLY|O_CLOEXEC|O_NOCTTY);
        if (fd >= 0)
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
}
