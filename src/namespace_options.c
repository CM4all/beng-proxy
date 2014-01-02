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
namespace_options_unshare(const struct namespace_options *options)
{
    int unshare_flags = 0;
    if (options->enable_user)
        unshare_flags |= CLONE_NEWUSER;
    if (options->enable_network)
        unshare_flags |= CLONE_NEWNET;

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
