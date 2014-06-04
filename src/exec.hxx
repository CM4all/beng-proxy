/*
 * Wrapper for execve().  Allows building the argument list
 * dynamically, and automatically de-consts the argument strings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_EXEC_HXX
#define BENG_PROXY_EXEC_HXX

#include <assert.h>
#include <string.h>
#include <unistd.h>

struct exec {
    char *args[32];
    unsigned num_args;

    void Init() {
        num_args = 0;
    }

    void Append(const char *arg) {
        assert(arg != nullptr);

        /* for whatever reason, execve() wants non-const string
           pointers - this is a hack to work around that limitation */
        union {
            const char *in;
            char *out;
        } u = { .in = arg };

        args[num_args++] = u.out;
    }

    void DoExec() {
        assert(num_args > 0);

        args[num_args] = nullptr;

        const char *path = args[0];
        char *slash = strrchr(path, '/');
        if (slash != nullptr && slash[1] != 0)
            args[0] = slash + 1;

        execv(path, args);
    }
};

#endif
