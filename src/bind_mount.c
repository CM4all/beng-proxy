/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bind_mount.h"

#include <sys/mount.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

void
bind_mount(const char *source, const char *target, int flags)
{
    if (mount(source, target, NULL, MS_BIND|flags, NULL) < 0) {
        fprintf(stderr, "bind_mount('%s', '%s') failed: %s\n",
                source, target, strerror(errno));
        _exit(2);
    }
}
