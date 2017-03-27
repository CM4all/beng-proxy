/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Isolate.hxx"
#include "system/pivot_root.h"
#include "io/WriteFile.hxx"
#include "util/ScopeExit.hxx"

#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>

#ifndef __linux
#error This library requires Linux
#endif

static WriteFileResult
setup_uid_map(int uid)
{
    char buffer[64];
    sprintf(buffer, "%d %d 1", uid, uid);
    return TryWriteExistingFile("/proc/self/uid_map", buffer);
}

static WriteFileResult
setup_gid_map(int gid)
{
    char buffer[64];
    sprintf(buffer, "%d %d 1", gid, gid);
    return TryWriteExistingFile("/proc/self/gid_map", buffer);
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
isolate_from_filesystem(bool allow_dbus)
{
    const int uid = geteuid(), gid = getegid();

    constexpr int flags = CLONE_NEWUSER|CLONE_NEWNS;
    if (unshare(flags) < 0) {
        fprintf(stderr, "unshare(0x%x) failed: %s\n", flags, strerror(errno));
        return;
    }

    /* since version 4.8, the Linux kernel requires a uid/gid mapping
       or else the mkdir() calls below fail */
    /* for dbus "AUTH EXTERNAL", libdbus needs to obtain the "real"
       uid from geteuid(), so set up the mapping */
    deny_setgroups();
    setup_gid_map(gid);
    setup_uid_map(uid);

    /* convert all "shared" mounts to "private" mounts */
    mount(nullptr, "/", nullptr, MS_PRIVATE|MS_REC, nullptr);

    const char *const new_root = "/tmp";
    const char *const put_old = "old";

    if (mount(nullptr, new_root, "tmpfs", MS_NODEV|MS_NOEXEC|MS_NOSUID,
              "size=16k,nr_inodes=16,mode=700") < 0) {
        fprintf(stderr, "failed to mount tmpfs: %s\n", strerror(errno));
        return;
    }

    /* release a reference to the old root */
    if (chdir(new_root) < 0) {
        fprintf(stderr, "chdir('%s') failed: %s\n",
                new_root, strerror(errno));
        _exit(2);
    }

    /* bind-mount /run/systemd to be able to send messages to
       /run/systemd/notify */
    mkdir("run", 0700);

    mkdir("run/systemd", 0);
    mount("/run/systemd", "run/systemd", nullptr, MS_BIND, nullptr);
    mount(nullptr, "run/systemd", nullptr,
          MS_REMOUNT|MS_BIND|MS_NOEXEC|MS_NOSUID|MS_RDONLY, nullptr);

    if (allow_dbus) {
        mkdir("run/dbus", 0);
        mount("/run/dbus", "run/dbus", nullptr, MS_BIND, nullptr);
        mount(nullptr, "run/dbus", nullptr,
              MS_REMOUNT|MS_BIND|MS_NOEXEC|MS_NOSUID|MS_RDONLY, nullptr);
    }

    chmod("run", 0111);

    /* symlink /var/run to /run, because some libraries such as
       libdbus use the old path */
    mkdir("var", 0700);
    symlink("/run", "var/run");
    chmod("var", 0111);

    struct stat st;
    if (stat("/var/lib/cm4all/save-core/incoming", &st) == 0 && S_ISDIR(st.st_mode)) {
        /* bind-mount the cm4all-save-core "incoming" directory so
           beng-lb can generate core dumps */

        mkdir("var/lib", 0700);
        mkdir("var/lib/cm4all", 0700);
        mkdir("var/lib/cm4all/save-core", 0700);
        mkdir("var/lib/cm4all/save-core/incoming", 0);

        mount("/var/lib/cm4all/save-core/incoming",
              "var/lib/cm4all/save-core/incoming",
              nullptr, MS_BIND, nullptr);
        mount(nullptr, "var/lib/cm4all/save-core/incoming", nullptr,
              MS_REMOUNT|MS_BIND|MS_NOEXEC|MS_NOSUID, nullptr);

        chmod("var/lib", 0111);
        chmod("var/lib/cm4all", 0111);
        chmod("var/lib/cm4all/save-core", 0111);
    }

    /* enter the new root */
    mkdir(put_old, 0);
    if (my_pivot_root(new_root, put_old) < 0) {
        fprintf(stderr, "pivot_root('%s') failed: %s\n",
                new_root, strerror(errno));
        _exit(2);
    }

    /* get rid of the old root */
    if (umount2(put_old, MNT_DETACH) < 0) {
        fprintf(stderr, "umount('%s') failed: %s",
                put_old, strerror(errno));
        _exit(2);
    }

    rmdir(put_old);

    chmod("/", 0111);
}
