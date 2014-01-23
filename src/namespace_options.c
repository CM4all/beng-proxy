/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "namespace_options.h"
#include "pool.h"
#include "pivot_root.h"

#include <assert.h>
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mount.h>

#ifndef __linux
#error This library requires Linux
#endif

void
namespace_options_init(struct namespace_options *options)
{
    options->enable_user = false;
    options->enable_network = false;
    options->enable_mount = false;
    options->mount_proc = false;
    options->pivot_root = NULL;
}

void
namespace_options_copy(struct pool *pool, struct namespace_options *dest,
                       const struct namespace_options *src)
{
    *dest = *src;

    dest->pivot_root = p_strdup_checked(pool, src->pivot_root);
}

gcc_pure
int
namespace_options_clone_flags(const struct namespace_options *options,
                              int flags)
{
    if (options->enable_user)
        flags |= CLONE_NEWUSER;
    if (options->enable_pid)
        flags |= CLONE_NEWPID;
    if (options->enable_network)
        flags |= CLONE_NEWNET;
    if (options->enable_mount)
        flags |= CLONE_NEWNS;

    return flags;
}

void
namespace_options_unshare(const struct namespace_options *options)
{
    int unshare_flags = namespace_options_clone_flags(options, 0);

    if (unshare_flags != 0 && unshare(unshare_flags) < 0) {
        fprintf(stderr, "unshare(0x%x) failed: %s\n",
                unshare_flags, strerror(errno));
        _exit(2);
    }
}

void
namespace_options_setup(const struct namespace_options *options)
{
    if (options->enable_mount)
        /* convert all "shared" mounts to "private" mounts */
        mount(NULL, "/", NULL, MS_PRIVATE|MS_REC, NULL);

    const char *const new_root = options->pivot_root;
    const char *const put_old = "mnt";

    if (new_root != NULL) {
        /* first bind-mount the new root onto itself to "unlock" the
           kernel's mount object (flag MNT_LOCKED) in our namespace;
           without this, the kernel would not allow an unprivileged
           process to pivot_root to it */
        if (mount(new_root, new_root, "none", MS_BIND, NULL) < 0) {
            fprintf(stderr, "mount('%s') failed: %s\n",
                    new_root, strerror(errno));
            _exit(2);
        }

        /* release a reference to the old root */
        if (chdir(new_root) < 0) {
            fprintf(stderr, "chdir('%s') failed: %s\n",
                    new_root, strerror(errno));
            _exit(2);
        }

        /* enter the new root */
        if (my_pivot_root(new_root, put_old) < 0) {
            fprintf(stderr, "pivot_root('%s') failed: %s\n",
                    new_root, strerror(errno));
            _exit(2);
        }
    }

    /* we must mount proc now before we umount the old filesystem,
       because the kernel allows mounting proc only if proc was
       previously visible in this namespace */
    if (options->mount_proc &&
        mount("none", "/proc", "proc", MS_RDONLY, NULL) < 0) {
        fprintf(stderr, "mount('/proc') failed: %s\n",
                strerror(errno));
        _exit(2);
    }

    if (new_root != NULL) {
        /* get rid of the old root */
        if (umount2(put_old, MNT_DETACH) < 0) {
            fprintf(stderr, "umount('%s') failed: %s",
                    put_old, strerror(errno));
            _exit(2);
        }
    }
}

char *
namespace_options_id(const struct namespace_options *options, char *p)
{
    if (options->enable_user)
        p = mempcpy(p, ";uns", 4);

    if (options->enable_network)
        p = mempcpy(p, ";nns", 4);

    if (options->enable_mount) {
        p = mempcpy(p, ";mns", 4);

        if (options->pivot_root != NULL) {
            p = mempcpy(p, ";pvr=", 5);
            p = stpcpy(p, options->pivot_root);
        }
    }

    return p;
}
