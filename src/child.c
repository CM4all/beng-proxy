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

#include <daemon/log.h>

#include <sys/signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static struct child *
find_child_by_pid(struct instance *instance, pid_t pid)
{
    struct child *child;

    for (child = (struct child*)instance->children.next;
         child != (struct child*)&instance->children;
         child = (struct child*)child->siblings.next)
        if (child->pid == pid)
            return child;

    return NULL;
}

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
child_event_callback(int fd __attr_unused, short event __attr_unused,
                     void *ctx)
{
    struct instance *instance = (struct instance*)ctx;
    pid_t pid;
    int status, exit_status;
    struct child *child;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        child = find_child_by_pid(instance, pid);
        if (child == NULL)
            continue;

        exit_status = WEXITSTATUS(status);

        if (WIFSIGNALED(status)) {
            daemon_log(1, "child %d died from signal %d%s\n",
                       pid, WTERMSIG(status),
                       WCOREDUMP(status) ? " (core dumped)" : "");
        } else if (exit_status == 0)
            daemon_log(1, "child %d exited with success\n",
                       pid);
        else
            daemon_log(1, "child %d exited with status %d\n",
                       pid, exit_status);

        list_remove(&child->siblings);
        assert(instance->num_children > 0);
        --instance->num_children;
    }

    if (list_empty(&instance->children))
        event_del(&instance->child_event);

    schedule_respawn(instance);

    pool_commit();
}

pid_t
create_child(struct instance *instance)
{
    pid_t pid;

    assert(instance->respawn_event.ev_events == 0);

    session_manager_event_del();

    pid = fork();
    if (pid < 0) {
        daemon_log(1, "fork() failed: %s\n", strerror(errno));
    } else if (pid == 0) {
        deinit_signals(instance);

        instance->config.num_workers = 0;

        event_del(&instance->child_event);

        if (!list_empty(&instance->children)) {
            event_del(&instance->child_event);
            do
                list_remove(instance->children.next);
            while (!list_empty(&instance->children));
            instance->num_children = 0;
        }

        if (instance->listener != NULL)
            listener_event_del(instance->listener);

        while (!list_empty(&instance->connections))
            close_connection((struct client_connection*)instance->connections.next);

        event_base_free(instance->event_base);
        instance->event_base = event_init();

        init_signals(instance);

        session_manager_init();

        if (instance->listener != NULL)
            listener_event_add(instance->listener);
    } else {
        struct child *child;

        session_manager_event_add();

        if (list_empty(&instance->children)) {
            event_set(&instance->child_event, SIGCHLD, EV_SIGNAL|EV_PERSIST,
                      child_event_callback, instance);
            event_add(&instance->child_event, NULL);
        }

        /* XXX leak */
        child = p_calloc(instance->pool, sizeof(*child));
        child->pid = pid;

        list_add(&child->siblings, &instance->children);
        ++instance->num_children;
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
