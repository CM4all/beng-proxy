/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NAMESPACE_OPTIONS_HXX
#define BENG_PROXY_NAMESPACE_OPTIONS_HXX

#include "translation/Features.hxx"

#include <inline/compiler.h>

class AllocatorPtr;
struct MountList;
struct SpawnConfig;
struct UidGid;
class MatchInfo;

struct NamespaceOptions {
    /**
     * Start the child process in a new user namespace?
     */
    bool enable_user = false;

    /**
     * Start the child process in a new PID namespace?
     */
    bool enable_pid = false;

    /**
     * Start the child process in a new network namespace?
     */
    bool enable_network = false;

    /**
     * Start the child process in a new IPC namespace?
     */
    bool enable_ipc = false;

    bool enable_mount = false;

    /**
     * Mount a new /proc?
     */
    bool mount_proc = false;

    const char *pivot_root = nullptr;

    const char *home = nullptr;

#if TRANSLATION_ENABLE_EXPAND
    const char *expand_home = nullptr;
#endif

    /**
     * Mount the given home directory?  Value is the mount point.
     */
    const char *mount_home = nullptr;

    /**
     * Mount a new tmpfs on /tmp?  A non-empty string specifies
     * additional mount options, such as "size=64M".
     */
    const char *mount_tmp_tmpfs = nullptr;

    const char *mount_tmpfs = nullptr;

    MountList *mounts = nullptr;

    /**
     * The hostname of the new UTS namespace.
     */
    const char *hostname = nullptr;

    NamespaceOptions() = default;
    NamespaceOptions(AllocatorPtr alloc, const NamespaceOptions &src);

#if TRANSLATION_ENABLE_EXPAND
    gcc_pure
    bool IsExpandable() const;

    /**
     * Throws std::runtime_error on error.
     */
    void Expand(AllocatorPtr alloc, const MatchInfo &match_info);
#endif

    gcc_pure
    int GetCloneFlags(const SpawnConfig &config, int flags) const;

    void Setup(const SpawnConfig &config, const UidGid &uid_gid) const;

    char *MakeId(char *p) const;
};

#endif
