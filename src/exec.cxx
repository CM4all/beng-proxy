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
    assert(!args.empty());

    args.push_back(nullptr);

    const char *path = args.front();
    const char *slash = strrchr(path, '/');
    if (slash != nullptr && slash[1] != 0)
        args.front() = const_cast<char *>(slash + 1);

    execv(path, args.raw());

    fprintf(stderr, "failed to execute %s: %s\n", path, strerror(errno));
    _exit(1);
}
