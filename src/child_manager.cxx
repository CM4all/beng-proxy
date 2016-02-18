/*
 * Central manager for child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child_manager.hxx"
#include "crash.hxx"
#include "pool.hxx"
#include "spawn/ExitListener.hxx"
#include "event/TimerEvent.hxx"
#include "event/SignalEvent.hxx"
#include "event/Callback.hxx"
#include "system/clock.h"
#include "util/DeleteDisposer.hxx"

#include <daemon/log.h>
#include <daemon/daemonize.h>

#include <boost/intrusive/set.hpp>

#include <string>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/resource.h>

struct ChildProcess
    : boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    const pid_t pid;

    const std::string name;

    /**
     * The monotonic clock when this child process was started
     * (registered in this library).
     */
    const uint64_t start_us;

    ExitListener *listener;

    /**
     * This timer is set up by child_kill_signal().  If the child
     * process hasn't exited after a certain amount of time, we send
     * SIGKILL.
     */
    TimerEvent kill_timeout_event;

    ChildProcess(pid_t _pid, const char *_name,
                 ExitListener *_listener)
        :pid(_pid), name(_name),
         start_us(now_us()),
         listener(_listener),
         kill_timeout_event(MakeSimpleEventCallback(ChildProcess,
                                                    KillTimeoutCallback),
                            this) {}

    void OnExit(int status, const struct rusage &rusage);

    void KillTimeoutCallback();

    struct Compare {
        bool operator()(const ChildProcess &a, const ChildProcess &b) const {
            return a.pid < b.pid;
        }

        bool operator()(const ChildProcess &a, pid_t b) const {
            return a.pid < b;
        }

        bool operator()(pid_t a, const ChildProcess &b) const {
            return a < b.pid;
        }
    };
};

static const struct timeval child_kill_timeout = {
    .tv_sec = 60,
    .tv_usec = 0,
};

static bool shutdown_flag = false;
static boost::intrusive::set<ChildProcess,
                             boost::intrusive::compare<ChildProcess::Compare>,
                             boost::intrusive::constant_time_size<true>> children;
static SignalEvent sigchld_event;

static ChildProcess *
find_child_by_pid(pid_t pid)
{
    auto i = children.find(pid, ChildProcess::Compare());
    if (i == children.end())
        return nullptr;

    return &*i;
}

static void
child_remove(ChildProcess &child)
{
    assert(!children.empty());

    child.kill_timeout_event.Cancel();

    children.erase(children.iterator_to(child));
    if (shutdown_flag && children.empty())
        children_event_del();
}

static void
child_abandon(ChildProcess &child)
{
    child_remove(child);
    delete &child;
}

gcc_pure
static double
timeval_to_double(const struct timeval *tv)
{
    return tv->tv_sec + tv->tv_usec / 1000000.;
}

inline void
ChildProcess::OnExit(int status, const struct rusage &rusage)
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

    daemon_log(6, "stats on '%s' (pid %d): %1.3fs elapsed, %1.3fs user, %1.3fs sys, %ld/%ld faults, %ld/%ld switches\n",
               name.c_str(), (int)pid,
               (now_us() - start_us) / 1000000.,
               timeval_to_double(&rusage.ru_utime),
               timeval_to_double(&rusage.ru_stime),
               rusage.ru_minflt, rusage.ru_majflt,
               rusage.ru_nvcsw, rusage.ru_nivcsw);

    if (listener != nullptr)
        listener->OnChildProcessExit(status);
}

inline void
ChildProcess::KillTimeoutCallback()
{
    daemon_log(3, "sending SIGKILL to child process '%s' (pid %d) due to timeout\n",
               name.c_str(), (int)pid);

    if (kill(pid, SIGKILL) < 0)
        daemon_log(1, "failed to kill child process '%s' (pid %d): %s\n",
                   name.c_str(), (int)pid, strerror(errno));
}

static void
child_event_callback(int fd gcc_unused, short event gcc_unused,
                     void *ctx gcc_unused)
{
    pid_t pid;
    int status;

    struct rusage rusage;
    while ((pid = wait4(-1, &status, WNOHANG, &rusage)) > 0) {
        if (daemonize_child_exited(pid, status))
            continue;

        ChildProcess *child = find_child_by_pid(pid);
        if (child != nullptr) {
            child_remove(*child);
            child->OnExit(status, rusage);
            delete child;
        }
    }

    pool_commit();
}

void
children_init()
{
    assert(!shutdown_flag);

    children_event_add();
}

void
children_deinit()
{
    children_event_del();
    shutdown_flag = false;
}

void
children_clear()
{
    children.clear_and_dispose(DeleteDisposer());
}

void
children_shutdown(void)
{
    shutdown_flag = true;

    if (children.empty())
        children_event_del();
}

void
children_event_add(void)
{
    assert(!shutdown_flag);

    sigchld_event.Set(SIGCHLD, child_event_callback, nullptr);
    sigchld_event.Add();
}

void
children_event_del(void)
{
    sigchld_event.Delete();
}

void
child_register(pid_t pid, const char *name,
               ExitListener *listener)
{
    assert(!shutdown_flag);
    assert(name != nullptr);

    daemon_log(5, "added child process '%s' (pid %d)\n", name, (int)pid);

    auto child = new ChildProcess(pid, name, listener);

    children.insert(*child);
}

void
child_kill_signal(pid_t pid, int signo)
{
    ChildProcess *child = find_child_by_pid(pid);

    assert(child != nullptr);
    assert(child->listener != nullptr);

    daemon_log(5, "sending %s to child process '%s' (pid %d)\n",
               strsignal(signo), child->name.c_str(), (int)pid);

    child->listener = nullptr;

    if (kill(pid, signo) < 0) {
        daemon_log(1, "failed to kill child process '%s' (pid %d): %s\n",
                   child->name.c_str(), (int)pid, strerror(errno));

        /* if we can't kill the process, we can't do much, so let's
           just ignore the process from now on and don't let it delay
           the shutdown */
        child_abandon(*child);
        return;
    }

    child->kill_timeout_event.Add(child_kill_timeout);
}

void
child_kill(pid_t pid)
{
    child_kill_signal(pid, SIGTERM);
}

unsigned
child_get_count(void)
{
    return children.size();
}
