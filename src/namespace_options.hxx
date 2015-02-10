/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NAMESPACE_OPTIONS_HXX
#define BENG_PROXY_NAMESPACE_OPTIONS_HXX

#include "glibfwd.hxx"

#include <inline/compiler.h>

struct pool;
struct MountList;

struct NamespaceOptions {
    /**
     * Start the child process in a new user namespace?
     */
    bool enable_user;

    /**
     * Start the child process in a new PID namespace?
     */
    bool enable_pid;

    /**
     * Start the child process in a new network namespace?
     */
    bool enable_network;

    bool enable_mount;

    /**
     * Mount a new /proc?
     */
    bool mount_proc;

    /**
     * Mount a new tmpfs on /tmp?
     */
    bool mount_tmp_tmpfs;

    const char *pivot_root;

    const char *home;

    /**
     * Mount the given home directory?  Value is the mount point.
     */
    const char *mount_home;

    MountList *mounts;

    /**
     * The hostname of the new UTS namespace.
     */
    const char *hostname;

    NamespaceOptions() = default;
    NamespaceOptions(struct pool *pool, const NamespaceOptions &src);

    void Init();

    void CopyFrom(struct pool &pool, const NamespaceOptions &src);

    gcc_pure
    bool IsExpandable() const;

    bool Expand(struct pool &pool, const GMatchInfo *match_info,
                GError **error_r);

    gcc_pure
    int GetCloneFlags(int flags) const;

    void Unshare() const;
    void Setup() const;

    char *MakeId(char *p) const;
};

/**
 * Global library initialization.  Call after daemonization.
 */
void
namespace_options_global_init(void);

#endif
