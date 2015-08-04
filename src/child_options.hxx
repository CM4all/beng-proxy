/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_OPTIONS_HXX
#define BENG_PROXY_CHILD_OPTIONS_HXX

#include "rlimit_options.hxx"
#include "namespace_options.hxx"
#include "JailParams.hxx"

/**
 * Options for launching a child process.
 */
struct ChildOptions {
    /**
     * An absolute path where STDERR output will be appended.
     */
    const char *stderr_path;
    const char *expand_stderr_path;

    struct rlimit_options rlimits;

    NamespaceOptions ns;

    JailParams jail;

    ChildOptions() = default;
    ChildOptions(struct pool *pool, const ChildOptions &src);

    void Init() {
        stderr_path = nullptr;
        expand_stderr_path = nullptr;
        rlimits.Init();
        ns.Init();
        jail.Init();
    }

    void CopyFrom(struct pool *pool, const ChildOptions *src);

    bool Check(GError **error_r) const {
        return jail.Check(error_r);
    }

    bool IsExpandable() const {
        return expand_stderr_path != nullptr ||
            ns.IsExpandable() || jail.IsExpandable();
    }

    bool Expand(struct pool &pool, const GMatchInfo *match_info,
                GError **error_r);

    char *MakeId(char *p) const;

    int OpenStderrPath() const;
    void SetupStderr(bool stdout=false) const;
};

#endif
