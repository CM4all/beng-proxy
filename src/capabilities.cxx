/*
 * Utilities for Linux capabilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "capabilities.hxx"

#include <glib.h>

#include <sys/capability.h>
#include <sys/prctl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static const cap_value_t keep_list[1] = {
    /* keep the KILL capability to be able to kill child processes
       that have switched to another uid (e.g. via JailCGI) */
    CAP_KILL,
};

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
capabilities_post_setuid(void)
{
    /* restore the KEEPCAPS flag */

    if (prctl(PR_SET_KEEPCAPS, 0) < 0) {
        fprintf(stderr, "prctl(PR_SET_KEEPCAPS) failed: %s\n",
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* now drop all capabilities but the ones we want */

    cap_t caps = cap_get_proc();
    if (caps == NULL) {
        fprintf(stderr, "cap_get_proc() failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    cap_clear(caps);

    if (cap_set_flag(caps, CAP_EFFECTIVE,
                     G_N_ELEMENTS(keep_list), keep_list, CAP_SET) < 0) {
        fprintf(stderr, "cap_set_flag() failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (cap_set_flag(caps, CAP_PERMITTED,
                     G_N_ELEMENTS(keep_list), keep_list, CAP_SET) < 0) {
        fprintf(stderr, "cap_set_flag() failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (cap_set_proc(caps) < 0) {
        fprintf(stderr, "cap_set_proc() failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    cap_free(caps);
}
