/*
 * Wrapper for execve().  Allows building the argument list
 * dynamically, and automatically de-consts the argument strings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "exec.hxx"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

void
Exec::DoExec()
{
    assert(num_args > 0);

    args[num_args] = nullptr;

    const char *path = args[0];
    char *slash = strrchr(path, '/');
    if (slash != nullptr && slash[1] != 0)
        args[0] = slash + 1;

    execv(path, args);

    fprintf(stderr, "failed to execute %s: %s\n", path, strerror(errno));
    _exit(1);
}
