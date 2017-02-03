/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Registry.hxx"
#include "ExitListener.hxx"
#include "util/DeleteDisposer.hxx"

#include <daemon/log.h>

#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/resource.h>

static constexpr struct timeval child_kill_timeout = {
    .tv_sec = 60,
    .tv_usec = 0,
};

ChildProcessRegistry::ChildProcess::ChildProcess(EventLoop &_event_loop,
                                                 pid_t _pid, const char *_name,
                                                 ExitListener *_listener)
    :pid(_pid), name(_name),
     start_time(std::chrono::steady_clock::now()),
     listener(_listener),
     kill_timeout_event(_event_loop, BIND_THIS_METHOD(KillTimeoutCallback)) {}

static constexpr double
timeval_to_double(const struct timeval &tv)
{
    return tv.tv_sec + tv.tv_usec / 1000000.;
}

void
ChildProcessRegistry::ChildProcess::OnExit(int status,
                                           const struct rusage &rusage)
{
    const int exit_status = WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        int level = 1;
        if (!WCOREDUMP(status) && WTERMSIG(status) == SIGTERM)
            level = 4;

        daemon_log(level,
                   "child process '%s' (pid %d) died from signal %d%s\n",
                   name.c_str(), (int)pid,
                   WTERMSIG(status),
                   WCOREDUMP(status) ? " (core dumped)" : "");
    } else if (exit_status == 0)
        daemon_log(5, "child process '%s' (pid %d) exited with success\n",
                   name.c_str(), (int)pid);
    else
        daemon_log(2, "child process '%s' (pid %d) exited with status %d\n",
                   name.c_str(), (int)pid, exit_status);

    const auto duration = std::chrono::steady_clock::now() - start_time;
    const auto duration_f = std::chrono::duration_cast<std::chrono::duration<double>>(duration);

    daemon_log(6, "stats on '%s' (pid %d): %1.3fs elapsed, %1.3fs user, %1.3fs sys, %ld/%ld faults, %ld/%ld switches\n",
               name.c_str(), (int)pid,
               duration_f.count(),
               timeval_to_double(rusage.ru_utime),
               timeval_to_double(rusage.ru_stime),
               rusage.ru_minflt, rusage.ru_majflt,
               rusage.ru_nvcsw, rusage.ru_nivcsw);

    if (listener != nullptr)
        listener->OnChildProcessExit(status);
}

inline void
ChildProcessRegistry::ChildProcess::KillTimeoutCallback()
{
    daemon_log(3, "sending SIGKILL to child process '%s' (pid %d) due to timeout\n",
               name.c_str(), (int)pid);

    if (kill(pid, SIGKILL) < 0)
        daemon_log(1, "failed to kill child process '%s' (pid %d): %s\n",
                   name.c_str(), (int)pid, strerror(errno));
}

ChildProcessRegistry::ChildProcessRegistry(EventLoop &_event_loop)
    :event_loop(_event_loop),
     sigchld_event(event_loop, SIGCHLD, BIND_THIS_METHOD(OnSigChld))
{
    sigchld_event.Enable();
}

void
ChildProcessRegistry::Clear()
{
    children.clear_and_dispose(DeleteDisposer());

    CheckVolatileEvent();
}

void
ChildProcessRegistry::Add(pid_t pid, const char *name, ExitListener *listener)
{
    assert(name != nullptr);

    if (volatile_event && IsEmpty())
        sigchld_event.Enable();

    daemon_log(5, "added child process '%s' (pid %d)\n", name, (int)pid);

    auto child = new ChildProcess(event_loop, pid, name, listener);

    children.insert(*child);
}

void
ChildProcessRegistry::SetExitListener(pid_t pid, ExitListener *listener)
{
    assert(pid > 0);
    assert(listener != nullptr);

    auto i = FindByPid(pid);
    assert(i != children.end());
    auto &child = *i;

    assert(child.listener == nullptr);
    child.listener = listener;
}

void
ChildProcessRegistry::Kill(pid_t pid, int signo)
{
    auto i = FindByPid(pid);
    assert(i != children.end());
    auto *child = &*i;

    daemon_log(5, "sending %s to child process '%s' (pid %d)\n",
               strsignal(signo), child->name.c_str(), (int)pid);

    assert(child->listener != nullptr);
    child->listener = nullptr;

    if (kill(pid, signo) < 0) {
        daemon_log(1, "failed to kill child process '%s' (pid %d): %s\n",
                   child->name.c_str(), (int)pid, strerror(errno));

        /* if we can't kill the process, we can't do much, so let's
           just ignore the process from now on and don't let it delay
           the shutdown */
        Remove(i);
        delete child;
        CheckVolatileEvent();
        return;
    }

    child->kill_timeout_event.Add(child_kill_timeout);
}

void
ChildProcessRegistry::Kill(pid_t pid)
{
    Kill(pid, SIGTERM);
}

void
ChildProcessRegistry::OnExit(pid_t pid, int status,
                             const struct rusage &rusage)
{
    auto i = FindByPid(pid);
    if (i == children.end())
        return;

    auto *child = &*i;
    Remove(i);
    child->OnExit(status, rusage);
    delete child;
}


void
ChildProcessRegistry::OnSigChld(int)
{
    pid_t pid;
    int status;

    struct rusage rusage;
    while ((pid = wait4(-1, &status, WNOHANG, &rusage)) > 0) {
        OnExit(pid, status, rusage);
    }

    CheckVolatileEvent();
}
