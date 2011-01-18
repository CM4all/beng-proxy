/*
 * Wrapper for execve().  Allows building the argument list
 * dynamically, and automatically de-consts the argument strings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_EXEC_H
#define BENG_PROXY_EXEC_H

#include <assert.h>
#include <string.h>
#include <unistd.h>

struct exec {
    char *args[32];
    unsigned num_args;
};

static inline void
exec_init(struct exec *e)
{
    assert(e != NULL);

    e->num_args = 0;
}

static inline void
exec_append(struct exec *e, const char *arg)
{
    assert(e != NULL);
    assert(arg != NULL);

    /* for whatever reason, execve() wants non-const string pointers -
       this is a hack to work around that limitation */
    union {
        const char *in;
        char *out;
    } u = { .in = arg };

    e->args[e->num_args++] = u.out;
}

static inline void
exec_do(struct exec *e)
{
    assert(e != NULL);
    assert(e->num_args > 0);

    e->args[e->num_args] = NULL;

    const char *path = e->args[0];
    char *slash = strrchr(path, '/');
    if (slash != NULL && slash[1] != 0)
        e->args[0] = slash + 1;

    execv(path, e->args);
}

#endif
