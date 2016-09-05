/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_OPTIONS_HXX
#define BENG_PROXY_CHILD_OPTIONS_HXX

#include "ExpandableStringList.hxx"
#include "CgroupOptions.hxx"
#include "RefenceOptions.hxx"
#include "NamespaceOptions.hxx"
#include "UidGid.hxx"
#include "util/ShallowCopy.hxx"
#include "glibfwd.hxx"

struct ResourceLimits;
struct JailParams;
struct PreparedChildProcess;
class MatchInfo;
class Error;

/**
 * Options for launching a child process.
 */
struct ChildOptions {
    /**
     * An absolute path where STDERR output will be appended.
     */
    const char *stderr_path = nullptr;
    const char *expand_stderr_path = nullptr;

    /**
     * Environment variables.
     */
    ExpandableStringList env;

    CgroupOptions cgroup;

    ResourceLimits *rlimits = nullptr;

    RefenceOptions refence;

    NamespaceOptions ns;

    JailParams *jail = nullptr;

    UidGid uid_gid;

    bool no_new_privs = false;

    ChildOptions() = default;

    constexpr ChildOptions(ShallowCopy shallow_copy, const ChildOptions &src)
        :stderr_path(src.stderr_path),
         expand_stderr_path(src.expand_stderr_path),
         env(shallow_copy, src.env),
         cgroup(src.cgroup),
         rlimits(src.rlimits),
         refence(src.refence),
         ns(src.ns),
         jail(src.jail),
         uid_gid(src.uid_gid),
         no_new_privs(src.no_new_privs) {}

    ChildOptions(struct pool *pool, const ChildOptions &src);

    ChildOptions(ChildOptions &&) = default;
    ChildOptions &operator=(ChildOptions &&) = default;

    bool Check(GError **error_r) const;

    gcc_pure
    bool IsExpandable() const;

    bool Expand(struct pool &pool, const MatchInfo &match_info,
                Error &error_r);

    char *MakeId(char *p) const;

    int OpenStderrPath() const;

    /**
     * @param use_jail shall #jail be used?  Pass false for protocols
     * which have a non-standard way of calling the JailCGI wrapper,
     * e.g. basic CGI
     */
    bool CopyTo(PreparedChildProcess &dest, bool use_jail,
                const char *document_root, GError **error_r) const;
};

#endif
