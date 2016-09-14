/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PREPARED_CHILD_PROCESS_HXX
#define PREPARED_CHILD_PROCESS_HXX

#include "util/StaticArray.hxx"
#include "CgroupOptions.hxx"
#include "ResourceLimits.hxx"
#include "RefenceOptions.hxx"
#include "NamespaceOptions.hxx"
#include "UidGid.hxx"

#include <string>
#include <forward_list>

#include <assert.h>

class UniqueFileDescriptor;
template<typename T> struct ConstBuffer;

struct PreparedChildProcess {
    StaticArray<const char *, 32> args;
    StaticArray<const char *, 32> env;
    int stdin_fd = -1, stdout_fd = -1, stderr_fd = -1, control_fd = -1;

    /**
     * The CPU scheduler priority configured with setpriority(),
     * ranging from -20 to 19.
     */
    int priority = 0;

    CgroupOptions cgroup;

    RefenceOptions refence;

    NamespaceOptions ns;

    ResourceLimits rlimits;

    UidGid uid_gid;

    /**
     * Change to this new root directory.  This feature should not be
     * used; use NamespaceOptions::pivot_root instead.  It is only
     * here for compatibility.
     */
    const char *chroot = nullptr;

    bool no_new_privs = false;

    /**
     * String allocations for SetEnv().
     */
    std::forward_list<std::string> strings;

    PreparedChildProcess();
    ~PreparedChildProcess();

    PreparedChildProcess(const PreparedChildProcess &) = delete;
    PreparedChildProcess &operator=(const PreparedChildProcess &) = delete;

    bool InsertWrapper(ConstBuffer<const char *> w);

    bool Append(const char *arg) {
        assert(arg != nullptr);

        if (args.size() + 1 >= env.capacity())
            return false;

        args.push_back(arg);
        return true;
    }

    bool PutEnv(const char *p) {
        if (env.size() + 1 >= env.capacity())
            return false;

        env.push_back(p);
        return true;
    }

    bool SetEnv(const char *name, const char *value);

    void SetStdin(int fd);
    void SetStdout(int fd);
    void SetStderr(int fd);
    void SetControl(int fd);

    void SetStdin(UniqueFileDescriptor &&fd);
    void SetStdout(UniqueFileDescriptor &&fd);
    void SetStderr(UniqueFileDescriptor &&fd);
    void SetControl(UniqueFileDescriptor &&fd);

    /**
     * Finish this object and return the executable path.
     */
    const char *Finish();
};

#endif
