/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PREPARED_CHILD_PROCESS_HXX
#define PREPARED_CHILD_PROCESS_HXX

#include "util/StaticArray.hxx"

#include <assert.h>

struct PreparedChildProcess {
    StaticArray<const char *, 32> args;
    StaticArray<const char *, 32> env;

    PreparedChildProcess() = default;

    PreparedChildProcess(const PreparedChildProcess &) = delete;

    void Append(const char *arg) {
        assert(arg != nullptr);

        args.push_back(arg);
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
};

#endif
