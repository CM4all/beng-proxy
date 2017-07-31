/*
 * Utilities for Linux capabilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "capabilities.hxx"

#include <sys/prctl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

void
capabilities_pre_setuid(void)
{
    /* we want to keep all capabilities after switching to an
       unprivileged uid */

    if (prctl(PR_SET_KEEPCAPS, 1) < 0) {
        fprintf(stderr, "prctl(PR_SET_KEEPCAPS) failed: %s\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void
capabilities_post_setuid(const cap_value_t *keep_list, unsigned n)
{
    /* restore the KEEPCAPS flag */

    if (prctl(PR_SET_KEEPCAPS, 0) < 0) {
        fprintf(stderr, "prctl(PR_SET_KEEPCAPS) failed: %s\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* now drop all capabilities but the ones we want */

    cap_t caps = cap_init();

    if (cap_set_flag(caps, CAP_EFFECTIVE, n, keep_list, CAP_SET) < 0) {
        fprintf(stderr, "cap_set_flag() failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (cap_set_flag(caps, CAP_PERMITTED, n, keep_list, CAP_SET) < 0) {
        fprintf(stderr, "cap_set_flag() failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (cap_set_proc(caps) < 0) {
        fprintf(stderr, "cap_set_proc() failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    cap_free(caps);
}
