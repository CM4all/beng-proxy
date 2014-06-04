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

    gcc_noreturn
    void DoExec();
};

#endif
