/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SPAWN_HXX
#define SPAWN_HXX

#include <inline/compiler.h>

#include <sys/types.h>

struct PreparedChildProcess;

/**
 * Wrapper for execve().  Allows building the argument list
 * dynamically, and automatically de-consts the argument strings.
 */
gcc_noreturn
void
Exec(PreparedChildProcess &&p);

/**
 * @return the process id, or a negative errno value
 */
pid_t
SpawnChildProcess(PreparedChildProcess &&params);

#endif
