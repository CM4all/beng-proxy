/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "NamespaceOptions.hxx"
#include "Config.hxx"
#include "mount_list.hxx"
#include "AllocatorPtr.hxx"
#include "system/pivot_root.h"
#include "system/bind_mount.h"
#include "io/WriteFile.hxx"
#include "util/ScopeExit.hxx"

#if TRANSLATION_ENABLE_EXPAND
#include "pexpand.hxx"
#endif

#include <assert.h>
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/prctl.h>

#ifndef __linux
#error This library requires Linux
#endif

NamespaceOptions::NamespaceOptions(AllocatorPtr alloc,
                                   const NamespaceOptions &src)
    :enable_user(src.enable_user),
     enable_pid(src.enable_pid),
     enable_network(src.enable_network),
     enable_ipc(src.enable_ipc),
     enable_mount(src.enable_mount),
     mount_proc(src.mount_proc),
     pivot_root(alloc.CheckDup(src.pivot_root)),
     home(alloc.CheckDup(src.home)),
#if TRANSLATION_ENABLE_EXPAND
     expand_home(alloc.CheckDup(src.expand_home)),
#endif
     mount_home(alloc.CheckDup(src.mount_home)),
     mount_tmp_tmpfs(alloc.CheckDup(src.mount_tmp_tmpfs)),
     mount_tmpfs(alloc.CheckDup(src.mount_tmpfs)),
     mounts(MountList::CloneAll(alloc, src.mounts)),
     hostname(alloc.CheckDup(src.hostname))
{
}

#if TRANSLATION_ENABLE_EXPAND

bool
NamespaceOptions::IsExpandable() const
{
    return expand_home != nullptr || MountList::IsAnyExpandable(mounts);
}

void
NamespaceOptions::Expand(AllocatorPtr alloc, const MatchInfo &match_info)
{
    if (expand_home != nullptr)
        home = expand_string_unescaped(alloc, expand_home, match_info);

    MountList::ExpandAll(alloc, mounts, match_info);
}

#endif

int
NamespaceOptions::GetCloneFlags(const SpawnConfig &config, int flags) const
{
    // TODO: rewrite the namespace_superuser workaround
    if (enable_user && !config.ignore_userns)
        flags |= CLONE_NEWUSER;
    if (enable_pid)
        flags |= CLONE_NEWPID;
    if (enable_network)
        flags |= CLONE_NEWNET;
    if (enable_ipc)
        flags |= CLONE_NEWIPC;
    if (enable_mount)
        flags |= CLONE_NEWNS;
    if (hostname != nullptr)
        flags |= CLONE_NEWUTS;

    return flags;
}

static void
write_file(const char *path, const char *data)
{
    if (TryWriteExistingFile(path, data) == WriteFileResult::ERROR) {
        fprintf(stderr, "write('%s') failed: %s\n",
                path, strerror(errno));
        _exit(2);
    }
}

static void
setup_uid_map(int uid)
{
    char buffer[64];
    sprintf(buffer, "%d %d 1", uid, uid);
    write_file("/proc/self/uid_map", buffer);
}

static void
setup_gid_map(int gid)
{
    char buffer[64];
    sprintf(buffer, "%d %d 1", gid, gid);
    write_file("/proc/self/gid_map", buffer);
}

/**
 * Write "deny" to /proc/self/setgroups which is necessary for
 * unprivileged processes to set up a gid_map.  See Linux commits
 * 9cc4651 and 66d2f33 for details.
 */
static void
deny_setgroups()
{
    TryWriteExistingFile("/proc/self/setgroups", "deny");
}

void
NamespaceOptions::Setup(const SpawnConfig &config,
                        const UidGid &_uid_gid) const
{
    /* set up UID/GID mapping in the old /proc */
    if (enable_user && !config.ignore_userns) {
        // TODO: rewrite the namespace_superuser workaround
        deny_setgroups();

        const auto &uid_gid = !_uid_gid.IsEmpty()
            ? _uid_gid
            : config.default_uid_gid;

        if (uid_gid.gid != 0)
            setup_gid_map(uid_gid.gid);
        // TODO: map the current effective gid if no gid was given?

        setup_uid_map(uid_gid.uid);
    }

    if (enable_mount)
        /* convert all "shared" mounts to "private" mounts */
        mount(nullptr, "/", nullptr, MS_PRIVATE|MS_REC, nullptr);

    const char *const new_root = pivot_root;
    const char *const put_old = "mnt";

    if (new_root != nullptr) {
        /* first bind-mount the new root onto itself to "unlock" the
           kernel's mount object (flag MNT_LOCKED) in our namespace;
           without this, the kernel would not allow an unprivileged
           process to pivot_root to it */
        bind_mount(new_root, new_root, MS_NOSUID|MS_RDONLY);

        /* release a reference to the old root */
        if (chdir(new_root) < 0) {
            fprintf(stderr, "chdir('%s') failed: %s\n",
                    new_root, strerror(errno));
            _exit(2);
        }

        /* enter the new root */
        int result = my_pivot_root(new_root, put_old);
        if (result < 0) {
            fprintf(stderr, "pivot_root('%s') failed: %s\n",
                    new_root, strerror(-result));
            _exit(2);
        }
    }

    if (mount_proc &&
        mount("none", "/proc", "proc", MS_NOEXEC|MS_NOSUID|MS_NODEV|MS_RDONLY, nullptr) < 0) {
        fprintf(stderr, "mount('/proc') failed: %s\n",
                strerror(errno));
        _exit(2);
    }

    if (mount_home != nullptr || mounts != nullptr) {
        /* go to /mnt so we can refer to the old directories with a
           relative path */

        const char *path = new_root != nullptr ? "/mnt" : "/";

        if (chdir(path) < 0) {
            fprintf(stderr, "chdir('%s') failed: %s\n", path, strerror(errno));
            _exit(2);
        }
    }

    if (mount_home != nullptr) {
        assert(home != nullptr);
        assert(*home == '/');

        bind_mount(home + 1, mount_home, MS_NOSUID|MS_NODEV);
    }

    MountList::ApplyAll(mounts);

    if (new_root != nullptr && (mount_home != nullptr || mounts != nullptr)) {
        /* back to the new root */
        if (chdir("/") < 0) {
            fprintf(stderr, "chdir('/') failed: %s\n", strerror(errno));
            _exit(2);
        }
    }

    if (new_root != nullptr) {
        /* get rid of the old root */
        if (umount2(put_old, MNT_DETACH) < 0) {
            fprintf(stderr, "umount('%s') failed: %s",
                    put_old, strerror(errno));
            _exit(2);
        }
    }

    if (mount_tmpfs != nullptr &&
        mount("none", mount_tmpfs, "tmpfs", MS_NODEV|MS_NOEXEC|MS_NOSUID,
              "size=16M,nr_inodes=256,mode=700") < 0) {
        fprintf(stderr, "mount(tmpfs, '%s') failed: %s\n",
                mount_tmpfs, strerror(errno));
        _exit(2);
    }

    if (mount_tmp_tmpfs != nullptr) {
        const char *options = "size=16M,nr_inodes=256,mode=1777";
        char buffer[256];
        if (*mount_tmp_tmpfs != 0) {
            snprintf(buffer, sizeof(buffer), "%s,%s", options, mount_tmp_tmpfs);
            options = buffer;
        }

        if (mount("none", "/tmp", "tmpfs", MS_NODEV|MS_NOEXEC|MS_NOSUID,
                  options) < 0) {
            fprintf(stderr, "mount('/tmp') failed: %s\n",
                    strerror(errno));
            _exit(2);
        }
    }

    if (hostname != nullptr &&
        sethostname(hostname, strlen(hostname)) < 0) {
        fprintf(stderr, "sethostname() failed: %s", strerror(errno));
        _exit(2);
    }
}

char *
NamespaceOptions::MakeId(char *p) const
{
    if (enable_user)
        p = (char *)mempcpy(p, ";uns", 4);

    if (enable_pid)
        p = (char *)mempcpy(p, ";pns", 4);

    if (enable_network)
        p = (char *)mempcpy(p, ";nns", 4);

    if (enable_ipc)
        p = (char *)mempcpy(p, ";ins", 4);

    if (enable_mount) {
        p = (char *)(char *)mempcpy(p, ";mns", 4);

        if (pivot_root != nullptr) {
            p = (char *)mempcpy(p, ";pvr=", 5);
            p = stpcpy(p, pivot_root);
        }

        if (mount_proc)
            p = (char *)mempcpy(p, ";proc", 5);

        if (mount_home != nullptr) {
            p = (char *)mempcpy(p, ";h:", 3);
            p = stpcpy(p, home);
            *p++ = '=';
            p = stpcpy(p, mount_home);
        }

        if (mount_tmp_tmpfs != nullptr) {
            p = (char *)mempcpy(p, ";tt:", 3);
            p = stpcpy(p, mount_tmp_tmpfs);
        }

        if (mount_tmpfs != nullptr) {
            p = (char *)mempcpy(p, ";t:", 3);
            p = stpcpy(p, mount_tmpfs);
        }
    }

    if (hostname != nullptr) {
        p = (char *)mempcpy(p, ";uts=", 5);
        p = stpcpy(p, hostname);
    }

    return p;
}
