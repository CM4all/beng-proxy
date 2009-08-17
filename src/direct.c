/*
 * Helper inline functions for direct data transfer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "direct.h"

#include <unistd.h>
#include <errno.h>

#ifdef __linux
#ifdef SPLICE

static int dev_null = -1;

/** is splicing between two pipes supported by the kernel? */
static bool pipe_to_pipe_supported = true;

ssize_t
istream_direct_pipe_to_pipe(int src_fd, int dest_fd, size_t max_length)
{
    ssize_t nbytes;

    if (pipe_to_pipe_supported) {
        nbytes = splice(src_fd, NULL, dest_fd, NULL, max_length,
                        /* SPLICE_F_NONBLOCK | */ SPLICE_F_MORE | SPLICE_F_MOVE);
        if (nbytes != -1 || errno != EINVAL)
            return nbytes;

        pipe_to_pipe_supported = false;
    }

    /* splice() between two pipes is not supported by the kernel:
       tee() should always work though, because it is only defined for
       pipes.  We play a trick now: */

    if (dev_null < 0) {
        dev_null = open("/dev/null", O_WRONLY);
        if (dev_null < 0)
            return -1;
    }

    /* first duplicate the buffers with tee() .. */
    nbytes = tee(src_fd, dest_fd, max_length,
                 /* SPLICE_F_NONBLOCK | */ SPLICE_F_MORE);
    if (nbytes <= 0)
        return nbytes;

    /* .. then discard the original version with splice() to
       /dev/null */

    /* XXX check for splice() errors? */
    splice(src_fd, NULL, dev_null, NULL, (size_t)nbytes, SPLICE_F_MOVE);

    /* for a better solution, see
       http://lkml.org/lkml/2009/4/30/164 */

    return nbytes;
}

#endif /* #ifdefSPLICE */
#endif  /* #ifdef __linux */
