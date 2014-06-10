/*
 * Wrapper for execve().  Allows building the argument list
 * dynamically, and automatically de-consts the argument strings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "exec.hxx"

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

void
Exec::SetEnv(const char *name, const char *value)
{
    assert(name != nullptr);
    assert(value != nullptr);

    const size_t name_length = strlen(name);
    const size_t value_length = strlen(name);

    assert(name_length > 0);

    char *buffer = (char *)malloc(name_length + 1 + value_length + 1);
    memcpy(buffer, name, name_length);
    buffer[name_length] = '=';
    memcpy(buffer + name_length + 1, value, value_length);
    buffer[name_length + 1 + value_length] = 0;

    PutEnv(buffer);

    /* no need to free this allocation; this process will be replaced
       soon by execve() anyway */
}

void
Exec::DoExec()
{
    assert(!args.empty());

    args.push_back(nullptr);

    const char *path = args.front();
    const char *slash = strrchr(path, '/');
    if (slash != nullptr && slash[1] != 0)
        args.front() = const_cast<char *>(slash + 1);

    env.push_back(nullptr);

    execve(path, args.raw(), env.raw());

    fprintf(stderr, "failed to execute %s: %s\n", path, strerror(errno));
    _exit(1);
}
