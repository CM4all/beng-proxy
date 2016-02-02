/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SPAWN_HXX
#define SPAWN_HXX

#include <inline/compiler.h>

struct PreparedChildProcess;

/**
 * Wrapper for execve().  Allows building the argument list
 * dynamically, and automatically de-consts the argument strings.
 */
gcc_noreturn
void
Exec(PreparedChildProcess &&p);

#endif
