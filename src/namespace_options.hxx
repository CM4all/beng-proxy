/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NAMESPACE_OPTIONS_HXX
#define BENG_PROXY_NAMESPACE_OPTIONS_HXX

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
};

/**
 * Global library initialization.  Call after daemonization.
 */
void
namespace_options_global_init(void);

void
namespace_options_init(NamespaceOptions *options);

void
namespace_options_copy(struct pool *pool, NamespaceOptions *dest,
                       const NamespaceOptions *src);

gcc_pure
int
namespace_options_clone_flags(const NamespaceOptions *options,
                              int flags);

void
namespace_options_unshare(const NamespaceOptions *options);

void
namespace_options_setup(const NamespaceOptions *options);

char *
namespace_options_id(const NamespaceOptions *options, char *p);

#endif
