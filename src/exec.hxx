/*
 * Wrapper for execve().  Allows building the argument list
 * dynamically, and automatically de-consts the argument strings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_EXEC_HXX
#define BENG_PROXY_EXEC_HXX

#include <assert.h>

class Exec {
    char *args[32];
    unsigned num_args;

public:
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

    const char *GetPath() const {
        return args[0];
    }

    void DoExec();
};

#endif
