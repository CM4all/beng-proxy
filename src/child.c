/*
 * Central manager for child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child.h"
#include "crash.h"
#include "pool.h"
#include "defer_event.h"

#include <daemon/log.h>
#include <daemon/daemonize.h>
#include <inline/list.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <event.h>

struct child {
    struct list_head siblings;

    pid_t pid;

    const char *name;

    child_callback_t callback;
    void *callback_ctx;

    /**
     * This timer is set up by child_kill_signal().  If the child
     * process hasn't exited after a certain amount of time, we send
     * SIGKILL.
     */
    struct event kill_timeout_event;
};

static const struct timeval child_kill_timeout = {
    .tv_sec = 60,
    .tv_usec = 0,
};

static bool shutdown_flag = false;
static struct pool *pool;
static struct list_head children;
static unsigned num_children;
static struct event sigchld_event;

/**
 * This event is used by children_event_add() to invoke
 * child_event_callback() as soon as possible.  It is necessary to
 * catch up with SIGCHLDs that may have been lost while the SIGCHLD
 * handler was disabled.
 */
static struct defer_event defer_event;

static struct child *
find_child_by_pid(pid_t pid)
{
    assert(list_empty(&children) == (num_children == 0));

    struct child *child;

    for (child = (struct child*)children.next;
         &child->siblings != &children;
         child = (struct child*)child->siblings.next)
        if (child->pid == pid)
            return child;

    return NULL;
}

static void
child_free(struct child *child)
{
    p_free(pool, child->name);
    p_free(pool, child);
}

static void
child_remove(struct child *child)
{
    assert(num_children > 0);
    --num_children;

    evtimer_del(&child->kill_timeout_event);

    list_remove(&child->siblings);
    if (shutdown_flag && list_empty(&children)) {
        assert(num_children == 0);
        children_event_del();
    }
}

static void
child_abandon(struct child *child)
{
    child_remove(child);
    child_free(child);
}

static void
child_done(struct child *child, int status)
{
    const int exit_status = WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        int level = 1;
        if (!WCOREDUMP(status) && WTERMSIG(status) == SIGTERM)
            level = 4;

        daemon_log(level,
                   "child process '%s' (pid %d) died from signal %d%s\n",
                   child->name, (int)child->pid,
                   WTERMSIG(status),
                   WCOREDUMP(status) ? " (core dumped)" : "");
    } else if (exit_status == 0)
        daemon_log(5, "child process '%s' (pid %d) exited with success\n",
                   child->name, (int)child->pid);
    else
        daemon_log(2, "child process '%s' (pid %d) exited with status %d\n",
                   child->name, (int)child->pid, exit_status);

    child_remove(child);

    if (child->callback != NULL)
        child->callback(status, child->callback_ctx);
    child_free(child);
}

static void
child_kill_timeout_callback(gcc_unused int fd, gcc_unused short event,
                            void *ctx)
{
    struct child *child = ctx;

    daemon_log(3, "sending SIGKILL to child process '%s' (pid %d) due to timeout\n",
               child->name, (int)child->pid);

    if (kill(child->pid, SIGKILL) < 0)
        daemon_log(1, "failed to kill child process '%s' (pid %d): %s\n",
                   child->name, (int)child->pid, strerror(errno));
}

static void
child_event_callback(int fd gcc_unused, short event gcc_unused,
                     void *ctx gcc_unused)
{
    assert(list_empty(&children) == (num_children == 0));

    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (daemonize_child_exited(pid, status))
            continue;

        struct child *child = find_child_by_pid(pid);
        if (child != NULL)
            child_done(child, status);
    }

    pool_commit();
}

void
children_init(struct pool *_pool)
{
    assert(!shutdown_flag);

    pool = _pool;

    list_init(&children);
    num_children = 0;

    defer_event_init(&defer_event, child_event_callback, NULL);
    children_event_add();
}

void
children_shutdown(void)
{
    assert(list_empty(&children) == (num_children == 0));

    defer_event_deinit(&defer_event);

    shutdown_flag = true;

    if (list_empty(&children))
        children_event_del();
}

void
children_event_add(void)
{
    assert(!shutdown_flag);

    event_set(&sigchld_event, SIGCHLD, EV_SIGNAL|EV_PERSIST,
              child_event_callback, NULL);
    event_add(&sigchld_event, NULL);

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
    assert(list_empty(&children) == (num_children == 0));

    daemon_log(5, "added child process '%s' (pid %d)\n", name, (int)pid);

    struct child *child = p_malloc(pool, sizeof(*child));

    child->pid = pid;
    child->name = p_strdup(pool, name);
    child->callback = callback;
    child->callback_ctx = ctx;
    list_add(&child->siblings, &children);
    ++num_children;

    evtimer_set(&child->kill_timeout_event,
                child_kill_timeout_callback, child);
}

void
child_kill_signal(pid_t pid, int signo)
{
    struct child *child = find_child_by_pid(pid);

    assert(child != NULL);
    assert(child->callback != NULL);

    daemon_log(5, "sending %s to child process '%s' (pid %d)\n",
               strsignal(signo), child->name, (int)pid);

    child->callback = NULL;

    if (kill(pid, signo) < 0) {
        daemon_log(1, "failed to kill child process '%s' (pid %d): %s\n",
                   child->name, (int)pid, strerror(errno));

        /* if we can't kill the process, we can't do much, so let's
           just ignore the process from now on and don't let it delay
           the shutdown */
        child_abandon(child);
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
    return num_children;
}
