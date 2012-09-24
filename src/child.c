/*
 * Central manager for child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child.h"
#include "crash.h"
#include "pool.h"

#include <daemon/log.h>
#include <inline/list.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <event.h>

struct child {
    struct list_head siblings;

    pid_t pid;

    child_callback_t callback;
    void *callback_ctx;
};

static const struct timeval immediately = {
    .tv_sec = 0,
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
static struct event defer_event;

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
    p_free(pool, child);
}

static void
child_remove(struct child *child)
{
    assert(num_children > 0);
    --num_children;

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
    child_remove(child);

    if (child->callback != NULL)
        child->callback(status, child->callback_ctx);
    child_free(child);
}

static void
child_event_callback(int fd gcc_unused, short event gcc_unused,
                     void *ctx gcc_unused)
{
    assert(list_empty(&children) == (num_children == 0));

    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
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

    children_event_add();
}

void
children_shutdown(void)
{
    assert(list_empty(&children) == (num_children == 0));

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
    evtimer_set(&defer_event, child_event_callback, NULL);
    evtimer_add(&defer_event, &immediately);
}

void
children_event_del(void)
{
    event_del(&sigchld_event);
    evtimer_del(&defer_event);

    /* reset the "shutdown" flag, so the test suite may initialize
       this library more than once */
    shutdown_flag = false;
}

void
child_register(pid_t pid, child_callback_t callback, void *ctx)
{
    assert(!shutdown_flag);
    assert(list_empty(&children) == (num_children == 0));

    struct child *child = p_malloc(pool, sizeof(*child));

    child->pid = pid;
    child->callback = callback;
    child->callback_ctx = ctx;
    list_add(&child->siblings, &children);
    ++num_children;
}

void
child_kill(pid_t pid)
{
    struct child *child = find_child_by_pid(pid);

    assert(child != NULL);
    assert(child->callback != NULL);

    child->callback = NULL;

    if (kill(pid, SIGTERM) < 0) {
        daemon_log(1, "failed to kill child process %d: %s\n",
                   (int)pid, strerror(errno));

        /* if we can't kill the process, we can't do much, so let's
           just ignore the process from now on and don't let it delay
           the shutdown */
        child_abandon(child);
    }
}

unsigned
child_get_count(void)
{
    return num_children;
}
