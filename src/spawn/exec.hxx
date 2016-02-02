/*
 * Wrapper for execve().  Allows building the argument list
 * dynamically, and automatically de-consts the argument strings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_EXEC_HXX
#define BENG_PROXY_EXEC_HXX

#include <inline/compiler.h>

struct PreparedChildProcess;

gcc_noreturn
void
Exec(PreparedChildProcess &&p);

#endif
