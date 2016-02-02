/*
 * Wrapper for execve().  Allows building the argument list
 * dynamically, and automatically de-consts the argument strings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "exec.hxx"
#include "Prepared.hxx"

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

void
Exec(PreparedChildProcess &&p)
{
    assert(!p.args.empty());

    p.args.push_back(nullptr);

    const char *path = p.args.front();
    const char *slash = strrchr(path, '/');
    if (slash != nullptr && slash[1] != 0)
        p.args.front() = slash + 1;

    p.env.push_back(nullptr);

    execve(path, const_cast<char *const*>(p.args.raw()),
           const_cast<char *const*>(p.env.raw()));

    fprintf(stderr, "failed to execute %s: %s\n", path, strerror(errno));
    _exit(1);
}
