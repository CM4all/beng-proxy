/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "isolate.hxx"
#include "system/pivot_root.h"
#include "util/ScopeExit.hxx"

#include <daemon/log.h>

#include <sched.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef __linux
#error This library requires Linux
#endif

static bool
try_write_file(const char *path, const char *data)
{
    int fd = open(path, O_WRONLY|O_CLOEXEC);
    if (fd < 0)
        return false;

    AtScopeExit(fd) { close(fd); };
    return write(fd, data, strlen(data)) > 0;
}

static void
setup_uid_map(int uid)
{
    char buffer[64];
    sprintf(buffer, "%d %d 1", uid, uid);
    try_write_file("/proc/self/uid_map", buffer);
}

static void
setup_gid_map(int gid)
{
    char buffer[64];
    sprintf(buffer, "%d %d 1", gid, gid);
    try_write_file("/proc/self/gid_map", buffer);
}

/**
 * Write "deny" to /proc/self/setgroups which is necessary for
 * unprivileged processes to set up a gid_map.  See Linux commits
 * 9cc4651 and 66d2f33 for details.
 */
static void
deny_setgroups()
{
    try_write_file("/proc/self/setgroups", "deny");
}

void
isolate_from_filesystem()
{
    const int uid = geteuid(), gid = getegid();

    constexpr int flags = CLONE_NEWUSER|CLONE_NEWNS;
    if (unshare(flags) < 0) {
        daemon_log(3, "unshare(0x%x) failed: %s\n", flags, strerror(errno));
        return;
    }

    /* since version 4.8, the Linux kernel requires a uid/gid mapping
       or else the mkdir() calls below fail */
    deny_setgroups();
    setup_gid_map(gid);
    setup_uid_map(uid);

    /* convert all "shared" mounts to "private" mounts */
    mount(nullptr, "/", nullptr, MS_PRIVATE|MS_REC, nullptr);

    const char *const new_root = "/tmp";
    const char *const put_old = "old";

    if (mount(nullptr, new_root, "tmpfs", MS_NODEV|MS_NOEXEC|MS_NOSUID,
              "size=16k,nr_inodes=16,mode=700") < 0) {
        daemon_log(3, "failed to mount tmpfs: %s\n", strerror(errno));
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

    chmod("run", 0111);

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
