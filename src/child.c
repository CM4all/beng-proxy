/*
 * Central manager for child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child.h"

#include <inline/list.h>

#include <assert.h>
#include <sys/wait.h>
#include <event.h>

struct child {
    struct list_head siblings;

    pool_t pool;
    pid_t pid;

    child_callback_t callback;
    void *callback_ctx;
};

static bool shutdown = false;
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
child_event_callback(int fd __attr_unused, short event __attr_unused,
                     void *ctx __attr_unused)
{
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        struct child *child = find_child_by_pid(pid);
        if (child == NULL)
            continue;

        list_remove(&child->siblings);
        if (shutdown && list_empty(&children))
            children_event_del();

        child->callback(status, child->callback_ctx);
        p_free(child->pool, child);
        pool_unref(child->pool);
    }

    pool_commit();
}

void
children_init(void)
{
    assert(!shutdown);

    list_init(&children);

    children_event_add();
}

void
children_shutdown(void)
{
    shutdown = true;

    if (list_empty(&children))
        children_event_del();
}

void
children_event_add(void)
{
    assert(!shutdown);

    event_set(&sigchld_event, SIGCHLD, EV_SIGNAL|EV_PERSIST,
              child_event_callback, NULL);
    event_add(&sigchld_event, NULL);
}

void
children_event_del(void)
{
    event_del(&sigchld_event);
}

void
child_register(pool_t pool, pid_t pid, child_callback_t callback, void *ctx)
{
    struct child *child = p_malloc(pool, sizeof(*child));

    assert(!shutdown);

    pool_ref(pool);

    child->pool = pool;
    child->pid = pid;
    child->callback = callback;
    child->callback_ctx = ctx;
    list_add(&child->siblings, &children);
}
