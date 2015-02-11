/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_OPTIONS_HXX
#define BENG_PROXY_CHILD_OPTIONS_HXX

#include "rlimit_options.hxx"
#include "namespace_options.hxx"
#include "jail.hxx"

/**
 * Options for launching a child process.
 */
struct ChildOptions {
    /**
     * An absolute path where STDERR output will be appended.
     */
    const char *stderr_path;

    struct rlimit_options rlimits;

    NamespaceOptions ns;

    struct jail_params jail;

    ChildOptions() = default;
    ChildOptions(struct pool *pool, const ChildOptions &src);

    void Init() {
        stderr_path = nullptr;
        rlimit_options_init(&rlimits);
        namespace_options_init(&ns);
        jail_params_init(&jail);
    }

    void CopyFrom(struct pool *pool, const ChildOptions *src);

    char *MakeId(char *p) const;

    int OpenStderrPath() const;
    void SetupStderr(bool stdout=false) const;
};

#endif
