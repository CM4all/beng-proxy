/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SPAWN_ISTREAM_HXX
#define SPAWN_ISTREAM_HXX

#include "child_manager.hxx"
#include "glibfwd.hxx"

struct pool;
struct PreparedChildProcess;
class Istream;

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
SpawnChildProcess(struct pool *pool, const char *name,
                  Istream *input, Istream **output_r,
                  PreparedChildProcess &&prepared,
                  child_callback_t callback, void *ctx,
                  GError **error_r);

#endif
