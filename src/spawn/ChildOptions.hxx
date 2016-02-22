/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_OPTIONS_HXX
#define BENG_PROXY_CHILD_OPTIONS_HXX

#include "param_array.hxx"
#include "ResourceLimits.hxx"
#include "RefenceOptions.hxx"
#include "NamespaceOptions.hxx"
#include "JailParams.hxx"
#include "UidGid.hxx"

class MatchInfo;
class Error;

/**
 * Options for launching a child process.
 */
struct ChildOptions {
    /**
     * An absolute path where STDERR output will be appended.
     */
    const char *stderr_path;
    const char *expand_stderr_path;

    /**
     * Environment variables.
     */
    struct param_array env;

    ResourceLimits rlimits;

    RefenceOptions refence;

    NamespaceOptions ns;

    JailParams jail;

    UidGid uid_gid;

    ChildOptions() = default;
    ChildOptions(struct pool *pool, const ChildOptions &src);

    void Init() {
        stderr_path = nullptr;
        expand_stderr_path = nullptr;
        env.Init();
        rlimits.Init();
        refence.Init();
        ns.Init();
        jail.Init();
        uid_gid.Init();
    }

    void CopyFrom(struct pool *pool, const ChildOptions *src);

    bool Check(GError **error_r) const {
        return jail.Check(error_r);
    }

    bool IsExpandable() const {
        return expand_stderr_path != nullptr ||
            env.IsExpandable() ||
            ns.IsExpandable() || jail.IsExpandable();
    }

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
