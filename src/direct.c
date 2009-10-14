/*
 * Helper inline functions for direct data transfer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "direct.h"

#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#ifdef __linux
#ifdef SPLICE

unsigned ISTREAM_TO_PIPE = ISTREAM_FILE;

/**
 * Checks whether the kernel supports splice() between the two
 * specified file handle types.
 */
static bool
splice_supported(int src, int dest)
{
    return splice(src, NULL, dest, NULL, 1, SPLICE_F_NONBLOCK) >= 0 ||
        errno != EINVAL;
}

void
direct_global_init(void)
{
    int a[2], b[2];

    /* create a pipe and feed some data into it */

    if (pipe(a) < 0)
        abort();

    /* check splice(pipe, pipe) */

    if (pipe(b) < 0)
        abort();

    if (splice_supported(a[0], b[1]))
        ISTREAM_TO_PIPE |= ISTREAM_PIPE;

    close(b[0]);
    close(b[1]);

    /* cleanup */

    close(a[0]);
    close(a[1]);
}

void
direct_global_deinit(void)
{
}

#endif /* #ifdefSPLICE */
#endif  /* #ifdef __linux */
