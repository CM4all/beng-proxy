// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <sys/types.h>

struct pool;
struct PreparedChildProcess;
class SpawnService;
class EventLoop;
class UnusedIstreamPtr;

/**
 * Wrapper for the fork() system call.  Forks a sub process, returns
 * its standard output stream as an istream, and optionally sends the
 * contents of another istream to the child's standard input.
 * Registers the child process and invokes a callback when it exits.
 *
 * Throws std::runtime_error on error.
 *
 * @param name a symbolic name for the process to be used in log
 * messages
 * @param input a stream which will be passed as standard input to the
 * new process; will be consumed or closed by this function in any
 * case
 * @return the output stream
 */
UnusedIstreamPtr
SpawnChildProcess(EventLoop &event_loop, struct pool *pool, const char *name,
		  UnusedIstreamPtr input,
		  PreparedChildProcess &&prepared,
		  SpawnService &spawn_service);
