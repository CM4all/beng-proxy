/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "namespace_options.h"

#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifndef __linux
#error This library requires Linux
#endif

void
namespace_options_init(struct namespace_options *options)
{
    options->enable_user = false;
    options->enable_network = false;
}

void
namespace_options_copy(struct namespace_options *dest,
                       const struct namespace_options *src)
{
    *dest = *src;
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

char *
namespace_options_id(const struct namespace_options *options, char *p)
{
    if (options->enable_user)
        p = mempcpy(p, ";uns", 4);

    if (options->enable_network)
        p = mempcpy(p, ";nns", 4);

    return p;
}
