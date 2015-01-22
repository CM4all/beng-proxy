/*
 * Fork a process and connect its stdin and stdout to istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FORK_H
#define __BENG_FORK_H

#include "child_manager.h"

#include <glib.h>

struct pool;
struct istream;

static inline GQuark
fork_quark(void)
{
    return g_quark_from_static_string("fork");
}

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Wrapper for the fork() system call.  Forks a sub process, returns
 * its standard output stream as an istream, and optionally sends the
 * contents of another istream to the child's standard input.
 * Registers the child process and invokes a callback when it exits.
 *
 * @param name a symbolic name for the process to be used in log
 * messages
 * @param input a stream which will be passed as standard input to the
 * new process; will be consumed or closed by this function in any
 * case
 * @return 0 in the child process, the child pid in the parent
 * process, or -1 on failure
 */
pid_t
beng_fork(struct pool *pool, const char *name,
          struct istream *input, struct istream **output_r,
          int clone_flags,
          int (*fn)(void *ctx), void *fn_ctx,
          child_callback_t callback, void *ctx,
          GError **error_r);

#ifdef __cplusplus
}
#endif

#endif
