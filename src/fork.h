/*
 * Fork a process and connect its stdin and stdout to istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FORK_H
#define __BENG_FORK_H

#include "istream.h"
#include "child.h"

#include <glib.h>

static inline GQuark
fork_quark(void)
{
    return g_quark_from_static_string("fork");
}

/**
 * Wrapper for the fork() system call.  Forks a sub process, returns
 * its standard output stream as an istream, and optionally sends the
 * contents of another istream to the child's standard input.
 * Registers the child process and invokes a callback when it exits.
 *
 * @return 0 in the child process, the child pid in the parent
 * process, or -1 on failure (in this case, #input has not been
 * consumed/closed)
 */
pid_t
beng_fork(pool_t pool, istream_t input, istream_t *output_r,
          child_callback_t callback, void *ctx,
          GError **error_r);

#endif
