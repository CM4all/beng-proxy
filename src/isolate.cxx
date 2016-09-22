/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "isolate.hxx"
#include "system/pivot_root.h"

#include <daemon/log.h>

#include <sched.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>

#ifndef __linux
#error This library requires Linux
#endif

void
isolate_from_filesystem()
{
    constexpr int flags = CLONE_NEWUSER|CLONE_NEWNS;
    if (unshare(flags) < 0) {
        daemon_log(3, "unshare(0x%x) failed: %s\n", flags, strerror(errno));
        return;
    }

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

    /* symlink /var/run to /run, because some libraries such as
       libdbus use the old path */
    mkdir("var", 0700);
    symlink("/run", "var/run");
    chmod("var", 0111);

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
