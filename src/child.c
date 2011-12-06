/*
 * Central manager for child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child.h"
#include "crash.h"
#include "pool.h"

#include <inline/list.h>

#include <assert.h>
#include <sys/wait.h>
#include <event.h>

struct child {
    struct list_head siblings;

    pid_t pid;

    child_callback_t callback;
    void *callback_ctx;
};

static bool shutdown_flag = false;
static struct pool *pool;
static struct list_head children;
static struct event sigchld_event;

static struct child *
find_child_by_pid(pid_t pid)
{
    struct child *child;

    for (child = (struct child*)children.next;
         &child->siblings != &children;
         child = (struct child*)child->siblings.next)
        if (child->pid == pid)
            return child;

    return NULL;
}

static void
child_event_callback(int fd gcc_unused, short event gcc_unused,
                     void *ctx gcc_unused)
{
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        struct child *child = find_child_by_pid(pid);
        if (child == NULL)
            continue;

        list_remove(&child->siblings);
        if (shutdown_flag && list_empty(&children))
            children_event_del();

        if (child->callback != NULL)
            child->callback(status, child->callback_ctx);
        p_free(pool, child);
    }

    pool_commit();
}

void
children_init(struct pool *_pool)
{
    assert(!shutdown_flag);

    pool = _pool;

    list_init(&children);

    children_event_add();
}

void
children_shutdown(void)
{
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
}

void
children_event_del(void)
{
    event_del(&sigchld_event);

    /* reset the "shutdown" flag, so the test suite may initialize
       this library more than once */
    shutdown_flag = false;
}

void
child_register(pid_t pid, child_callback_t callback, void *ctx)
{
    struct child *child = p_malloc(pool, sizeof(*child));

    assert(!shutdown_flag);

    child->pid = pid;
    child->callback = callback;
    child->callback_ctx = ctx;
    list_add(&child->siblings, &children);
}

void
child_clear(pid_t pid)
{
    struct child *child = find_child_by_pid(pid);

    assert(child != NULL);
    assert(child->callback != NULL);

    child->callback = NULL;
}

