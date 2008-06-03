/*
 * Child process management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server.h"
#include "pool.h"
#include "instance.h"
#include "connection.h"
#include "session.h"
#include "child.h"

#include <daemon/log.h>

#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static void
schedule_respawn(struct instance *instance);

static void
respawn_event_callback(int fd __attr_unused, short event __attr_unused,
                       void *ctx)
{
    struct instance *instance = (struct instance*)ctx;
    pid_t pid;

    instance->respawn_event.ev_events = 0;

    if (instance->should_exit ||
        instance->num_children >= instance->config.num_workers)
        return;

    daemon_log(2, "respawning child\n");

    pid = create_child(instance);
    if (pid != 0)
        schedule_respawn(instance);
}

static void
schedule_respawn(struct instance *instance)
{
    if (!instance->should_exit &&
        instance->num_children < instance->config.num_workers &&
        instance->respawn_event.ev_events == 0) {
        static struct timeval tv = {
            .tv_sec = 1,
            .tv_usec = 0,
        };

        evtimer_set(&instance->respawn_event, respawn_event_callback, instance);
        evtimer_add(&instance->respawn_event, &tv);
    }
}

static void
worker_child_callback(int status, void *ctx)
{
    struct child *child = ctx;
    struct instance *instance = child->instance;
    int exit_status = WEXITSTATUS(status);

    if (WIFSIGNALED(status)) {
        daemon_log(1, "child %d died from signal %d%s\n",
                   child->pid, WTERMSIG(status),
                   WCOREDUMP(status) ? " (core dumped)" : "");
    } else if (exit_status == 0)
        daemon_log(1, "child %d exited with success\n",
                   child->pid);
    else
        daemon_log(1, "child %d exited with status %d\n",
                   child->pid, exit_status);

    list_remove(&child->siblings);
    assert(instance->num_children > 0);
    --instance->num_children;

    schedule_respawn(instance);
}

pid_t
create_child(struct instance *instance)
{
    pid_t pid;

    assert(instance->respawn_event.ev_events == 0);

    deinit_signals(instance);
    children_event_del();

    pid = fork();
    if (pid < 0) {
        daemon_log(1, "fork() failed: %s\n", strerror(errno));
    } else if (pid == 0) {
        event_reinit(instance->event_base);

        instance->config.num_workers = 0;

        list_init(&instance->children);
        instance->num_children = 0;

        if (instance->listener != NULL)
            listener_event_del(instance->listener);

        while (!list_empty(&instance->connections))
            close_connection((struct client_connection*)instance->connections.next);

        init_signals(instance);
        children_init(instance->pool);

        session_manager_event_del();
        session_manager_init();

        if (instance->listener != NULL)
            listener_event_add(instance->listener);
    } else {
        struct child *child;

        /* XXX leak */
        child = p_calloc(instance->pool, sizeof(*child));
        child->instance = instance;
        child->pid = pid;

        list_add(&child->siblings, &instance->children);
        ++instance->num_children;

        init_signals(instance);
        children_event_add();

        child_register(pid, worker_child_callback, child);
    }

    return pid;
}

void
kill_children(struct instance *instance)
{
    struct child *child;
    int ret;

    for (child = (struct child*)instance->children.next;
         child != (struct child*)&instance->children;
         child = (struct child*)child->siblings.next) {
        ret = kill(child->pid, SIGTERM);
        if (ret < 0)
            daemon_log(1, "failed to kill child: %s\n", strerror(errno));
    }
}
