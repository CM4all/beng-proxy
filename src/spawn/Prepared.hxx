/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PREPARED_CHILD_PROCESS_HXX
#define PREPARED_CHILD_PROCESS_HXX

#include "util/StaticArray.hxx"

#include <assert.h>

template<typename T> struct ConstBuffer;

struct PreparedChildProcess {
    StaticArray<const char *, 32> args;
    StaticArray<const char *, 32> env;

    PreparedChildProcess() = default;

    PreparedChildProcess(const PreparedChildProcess &) = delete;
    PreparedChildProcess &operator=(const PreparedChildProcess &) = delete;

    bool InsertWrapper(ConstBuffer<const char *> w);

    void Append(const char *arg) {
        assert(arg != nullptr);

        args.push_back(arg);
    }

    void PutEnv(const char *p) {
        assert(p != nullptr);

        env.push_back(p);
    }

    void SetEnv(const char *name, const char *value);

    /**
     * Finish this object and return the executable path.
     */
    const char *Finish();
};

#endif
