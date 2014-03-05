/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "namespace_options.h"
#include "pivot_root.h"

#include <sched.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <daemon/log.h>

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

    mkdir(put_old, 0);

    /* enter the new root */
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
}
