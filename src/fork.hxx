/*
 * Fork a process and connect its stdin and stdout to istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FORK_HXX
#define BENG_PROXY_FORK_HXX

#include "child_manager.hxx"
#include "glibfwd.hxx"

struct pool;
struct istream;

/**
 * Wrapper for the fork() system call.  Forks a sub process, returns
 * its standard output stream as an istream, and optionally sends the
 * contents of another istream to the child's standard input.
 * Registers the child process and invokes a callback when it exits.
 *
 * @param name a symbolic name for the process to be used in log
 * messages
 * @return 0 in the child process, the child pid in the parent
 * process, or -1 on failure (in this case, #input has not been
 * consumed/closed)
 */
pid_t
beng_fork(struct pool *pool, const char *name,
          struct istream *input, struct istream **output_r,
          int clone_flags,
          int (*fn)(void *ctx), void *fn_ctx,
          child_callback_t callback, void *ctx,
          GError **error_r);

#endif
