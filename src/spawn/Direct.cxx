/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Direct.hxx"
#include "Prepared.hxx"
#include "Config.hxx"
#include "SeccompFilter.hxx"
#include "io/FileDescriptor.hxx"

#include <inline/compiler.h>

#include <systemd/sd-journal.h>

#include <exception>

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/prctl.h>

#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif

static void
CheckedDup2(FileDescriptor oldfd, FileDescriptor newfd)
{
    if (oldfd.IsDefined())
        oldfd.CheckDuplicate(newfd);
}

static void
CheckedDup2(int oldfd, int newfd)
{
    CheckedDup2(FileDescriptor(oldfd), FileDescriptor(newfd));
}

gcc_noreturn
static void
Exec(const char *path, const PreparedChildProcess &p,
     const SpawnConfig &config, const CgroupState &cgroup_state)
{
    p.cgroup.Apply(cgroup_state);
    p.refence.Apply();
    p.ns.Setup(config, p.uid_gid);
    p.rlimits.Apply();

    if (p.chroot != nullptr && chroot(p.chroot) < 0) {
        fprintf(stderr, "chroot('%s') failed: %s\n",
                p.chroot, strerror(errno));
        _exit(EXIT_FAILURE);
    }

    if (p.priority != 0 &&
        setpriority(PRIO_PROCESS, getpid(), p.priority) < 0) {
        fprintf(stderr, "setpriority() failed: %s\n", strerror(errno));
        _exit(EXIT_FAILURE);
    }

    if (!p.uid_gid.IsEmpty())
        p.uid_gid.Apply();
    else if (config.ignore_userns)
        config.default_uid_gid.Apply();

    if (p.no_new_privs)
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

    int stdout_fd = p.stdout_fd, stderr_fd = p.stderr_fd;
    if (stdout_fd < 0 || stderr_fd < 0) {
        /* if no log destination was specified, log to the systemd
           journal */
        int journal_fd = sd_journal_stream_fd(p.args.front(), LOG_INFO, true);
        if (stdout_fd < 0)
            stdout_fd = journal_fd;
        if (stderr_fd < 0)
            stderr_fd = journal_fd;
    }

    constexpr int CONTROL_FILENO = 3;
    CheckedDup2(p.stdin_fd, STDIN_FILENO);
    CheckedDup2(stdout_fd, STDOUT_FILENO);
    CheckedDup2(stderr_fd, STDERR_FILENO);
    CheckedDup2(p.control_fd, CONTROL_FILENO);

    setsid();

    try {
        SeccompFilter sf(SCMP_ACT_ALLOW);

        /* forbid a bunch of dangerous system calls */

        sf.AddRule(SCMP_ACT_KILL, SCMP_SYS(init_module));
        sf.AddRule(SCMP_ACT_KILL, SCMP_SYS(delete_module));
        sf.AddRule(SCMP_ACT_KILL, SCMP_SYS(reboot));
        sf.AddRule(SCMP_ACT_KILL, SCMP_SYS(settimeofday));
        sf.AddRule(SCMP_ACT_KILL, SCMP_SYS(adjtimex));
        sf.AddRule(SCMP_ACT_KILL, SCMP_SYS(swapon));
        sf.AddRule(SCMP_ACT_KILL, SCMP_SYS(swapoff));

        /* ptrace() is dangerous because it allows breaking out of
           namespaces */
        sf.AddRule(SCMP_ACT_KILL, SCMP_SYS(ptrace));

        sf.Load();
    } catch (const std::runtime_error &e) {
        fprintf(stderr, "Failed to setup seccomp filter for '%s': %s\n",
                path, e.what());
    }

    execve(path, const_cast<char *const*>(p.args.raw()),
           const_cast<char *const*>(p.env.raw()));

    fprintf(stderr, "failed to execute %s: %s\n", path, strerror(errno));
    _exit(EXIT_FAILURE);
}

struct SpawnChildProcessContext {
    const SpawnConfig &config;
    const PreparedChildProcess &params;
    const CgroupState &cgroup_state;

    const char *path;

    SpawnChildProcessContext(PreparedChildProcess &_params,
                             const SpawnConfig &_config,
                             const CgroupState &_cgroup_state)
        :config(_config), params(_params),
         cgroup_state(_cgroup_state),
         path(_params.Finish()) {}
};

static int
spawn_fn(void *_ctx)
{
    auto &ctx = *(SpawnChildProcessContext *)_ctx;

    try {
        Exec(ctx.path, ctx.params, ctx.config, ctx.cgroup_state);
    } catch (const std::exception &e) {
        fprintf(stderr, "%s\n", e.what());
        _exit(EXIT_FAILURE);
    }
}

pid_t
SpawnChildProcess(PreparedChildProcess &&params, const SpawnConfig &config,
                  const CgroupState &cgroup_state)
{
    int clone_flags = SIGCHLD;
    clone_flags = params.ns.GetCloneFlags(config, clone_flags);

    SpawnChildProcessContext ctx(params, config, cgroup_state);

    char stack[8192];
    long pid = clone(spawn_fn, stack + sizeof(stack), clone_flags, &ctx);
    if (pid < 0)
        pid = -errno;

    return pid;
}
