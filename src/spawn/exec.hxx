/*
 * Wrapper for execve().  Allows building the argument list
 * dynamically, and automatically de-consts the argument strings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_EXEC_HXX
#define BENG_PROXY_EXEC_HXX

#include "util/StaticArray.hxx"

#include <inline/compiler.h>

#include <assert.h>

class Exec {
    StaticArray<char *, 32> args;
    StaticArray<char *, 32> env;

public:
    void Append(const char *arg) {
        assert(arg != nullptr);

        /* for whatever reason, execve() wants non-const string
           pointers - this is a hack to work around that limitation */
        char *deconst = const_cast<char *>(arg);

        args.push_back(deconst);
    }

    const char *GetPath() const {
        return args.front();
    }

    void PutEnv(const char *p) {
        assert(p != nullptr);

        /* for whatever reason, execve() wants non-const string
           pointers - this is a hack to work around that limitation */
        char *deconst = const_cast<char *>(p);

        env.push_back(deconst);
    }

    void SetEnv(const char *name, const char *value);

    gcc_noreturn
    void DoExec();
};

#endif
