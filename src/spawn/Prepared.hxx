/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PREPARED_CHILD_PROCESS_HXX
#define PREPARED_CHILD_PROCESS_HXX

#include "util/StaticArray.hxx"

#include <string>
#include <forward_list>

#include <assert.h>

template<typename T> struct ConstBuffer;

struct PreparedChildProcess {
    StaticArray<const char *, 32> args;
    StaticArray<const char *, 32> env;

    /**
     * String allocations for SetEnv().
     */
    std::forward_list<std::string> strings;

    PreparedChildProcess() = default;

    PreparedChildProcess(const PreparedChildProcess &) = delete;
    PreparedChildProcess &operator=(const PreparedChildProcess &) = delete;

    bool InsertWrapper(ConstBuffer<const char *> w);

    void Append(const char *arg) {
        assert(arg != nullptr);

        args.push_back(arg);
    }

    bool PutEnv(const char *p) {
        if (env.size() + 1 < env.capacity())
            return false;

        env.push_back(p);
        return true;
    }

    bool SetEnv(const char *name, const char *value);

    /**
     * Finish this object and return the executable path.
     */
    const char *Finish();
};

#endif
