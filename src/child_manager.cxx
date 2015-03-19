/*
 * Central manager for child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child_manager.hxx"
#include "crash.hxx"
#include "pool.hxx"
#include "defer_event.h"
#include "clock.h"

#include <daemon/log.h>
#include <daemon/daemonize.h>

#include <boost/intrusive/set.hpp>

#include <string>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <event.h>

struct ChildProcess
    : boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    const pid_t pid;

    const std::string name;

    /**
     * The monotonic clock when this child process was started
     * (registered in this library).
     */
    const uint64_t start_us;

    child_callback_t callback;
    void *const callback_ctx;

    /**
     * This timer is set up by child_kill_signal().  If the child
     * process hasn't exited after a certain amount of time, we send
     * SIGKILL.
     */
    struct event kill_timeout_event;

    ChildProcess(pid_t _pid, const char *_name,
                 child_callback_t _callback, void *_ctx)
        :pid(_pid), name(_name),
         start_us(now_us()),
         callback(_callback), callback_ctx(_ctx) {}

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
static struct event sigchld_event;

/**
 * This event is used by children_event_add() to invoke
 * child_event_callback() as soon as possible.  It is necessary to
 * catch up with SIGCHLDs that may have been lost while the SIGCHLD
 * handler was disabled.
 */
static struct defer_event defer_event;

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

    evtimer_del(&child.kill_timeout_event);

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

static void
child_done(ChildProcess &child, int status, const struct rusage *rusage)
{
    const int exit_status = WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        int level = 1;
        if (!WCOREDUMP(status) && WTERMSIG(status) == SIGTERM)
            level = 4;

        daemon_log(level,
                   "child process '%s' (pid %d) died from signal %d%s\n",
                   child.name.c_str(), (int)child.pid,
                   WTERMSIG(status),
                   WCOREDUMP(status) ? " (core dumped)" : "");
    } else if (exit_status == 0)
        daemon_log(5, "child process '%s' (pid %d) exited with success\n",
                   child.name.c_str(), (int)child.pid);
    else
        daemon_log(2, "child process '%s' (pid %d) exited with status %d\n",
                   child.name.c_str(), (int)child.pid, exit_status);

    daemon_log(6, "stats on '%s' (pid %d): %1.3fs elapsed, %1.3fs user, %1.3fs sys, %ld/%ld faults, %ld/%ld switches\n",
               child.name.c_str(), (int)child.pid,
               (now_us() - child.start_us) / 1000000.,
               timeval_to_double(&rusage->ru_utime),
               timeval_to_double(&rusage->ru_stime),
               rusage->ru_minflt, rusage->ru_majflt,
               rusage->ru_nvcsw, rusage->ru_nivcsw);

    child_remove(child);

    if (child.callback != nullptr)
        child.callback(status, child.callback_ctx);
    delete &child;
}

static void
child_kill_timeout_callback(gcc_unused int fd, gcc_unused short event,
                            void *ctx)
{
    ChildProcess &child = *(ChildProcess *)ctx;

    daemon_log(3, "sending SIGKILL to child process '%s' (pid %d) due to timeout\n",
               child.name.c_str(), (int)child.pid);

    if (kill(child.pid, SIGKILL) < 0)
        daemon_log(1, "failed to kill child process '%s' (pid %d): %s\n",
                   child.name.c_str(), (int)child.pid, strerror(errno));
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
        if (child != nullptr)
            child_done(*child, status, &rusage);
    }

    pool_commit();
}

void
children_init()
{
    assert(!shutdown_flag);

    defer_event_init(&defer_event, child_event_callback, nullptr);
    children_event_add();
}

void
children_shutdown(void)
{
    defer_event_deinit(&defer_event);

    shutdown_flag = true;

    if (children.empty())
        children_event_del();
}

void
children_event_add(void)
{
    assert(!shutdown_flag);

    event_set(&sigchld_event, SIGCHLD, EV_SIGNAL|EV_PERSIST,
              child_event_callback, nullptr);
    event_add(&sigchld_event, nullptr);

    /* schedule an immediate waitpid() run, just in case we lost a
       SIGCHLD */
    defer_event_add(&defer_event);
}

void
children_event_del(void)
{
    event_del(&sigchld_event);
    defer_event_cancel(&defer_event);

    /* reset the "shutdown" flag, so the test suite may initialize
       this library more than once */
    shutdown_flag = false;
}

void
child_register(pid_t pid, const char *name,
               child_callback_t callback, void *ctx)
{
    assert(!shutdown_flag);
    assert(name != nullptr);

    daemon_log(5, "added child process '%s' (pid %d)\n", name, (int)pid);

    auto child = new ChildProcess(pid, name, callback, ctx);

    children.insert(*child);

    evtimer_set(&child->kill_timeout_event,
                child_kill_timeout_callback, child);
}

void
child_kill_signal(pid_t pid, int signo)
{
    ChildProcess *child = find_child_by_pid(pid);

    assert(child != nullptr);
    assert(child->callback != nullptr);

    daemon_log(5, "sending %s to child process '%s' (pid %d)\n",
               strsignal(signo), child->name.c_str(), (int)pid);

    child->callback = nullptr;

    if (kill(pid, signo) < 0) {
        daemon_log(1, "failed to kill child process '%s' (pid %d): %s\n",
                   child->name.c_str(), (int)pid, strerror(errno));

        /* if we can't kill the process, we can't do much, so let's
           just ignore the process from now on and don't let it delay
           the shutdown */
        child_abandon(*child);
        return;
    }

    evtimer_add(&child->kill_timeout_event, &child_kill_timeout);
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
