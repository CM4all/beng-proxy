/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_OPTIONS_HXX
#define BENG_PROXY_CHILD_OPTIONS_HXX

#include "translation/Features.hxx"
#include "adata/ExpandableStringList.hxx"
#include "CgroupOptions.hxx"
#include "RefenceOptions.hxx"
#include "NamespaceOptions.hxx"
#include "UidGid.hxx"
#include "util/ShallowCopy.hxx"

struct ResourceLimits;
struct JailParams;
struct PreparedChildProcess;
class MatchInfo;

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

#if TRANSLATION_ENABLE_JAILCGI
    JailParams *jail = nullptr;
#endif

    UidGid uid_gid;

    /**
     * Redirect STDERR to /dev/null?
     */
    bool stderr_null = false;

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
#if TRANSLATION_ENABLE_JAILCGI
         jail(src.jail),
#endif
         uid_gid(src.uid_gid),
         stderr_null(src.stderr_null),
         no_new_privs(src.no_new_privs) {}

    ChildOptions(AllocatorPtr alloc, const ChildOptions &src);

    ChildOptions(ChildOptions &&) = default;
    ChildOptions &operator=(ChildOptions &&) = default;

    /**
     * Throws std::runtime_error on error.
     */
    void Check() const;

    gcc_pure
    bool IsExpandable() const;

    void Expand(AllocatorPtr alloc, const MatchInfo &match_info);

    char *MakeId(char *p) const;

    int OpenStderrPath() const;

    /**
     * Throws std::runtime_error on error.
     *
     * @param use_jail shall #jail be used?  Pass false for protocols
     * which have a non-standard way of calling the JailCGI wrapper,
     * e.g. basic CGI
     */
    void CopyTo(PreparedChildProcess &dest
#if TRANSLATION_ENABLE_JAILCGI
                , bool use_jail, const char *document_root
#endif
                ) const;
};

#endif
