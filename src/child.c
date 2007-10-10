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
child_event_callback(int fd, short event, void *ctx)
{
    struct instance *instance = (struct instance*)ctx;
    pid_t pid;
    int status, exit_status;
    struct child *child;

    (void)fd;
    (void)event;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        child = find_child_by_pid(instance, pid);
        if (child == NULL)
            continue;

        exit_status = WEXITSTATUS(status);

        if (WIFSIGNALED(status)) {
            daemon_log(1, "child %d died from signal %d%s\n",
                       pid, WTERMSIG(status),
                       WCOREDUMP(status) ? " (core dumped)" : "");
            exit_status = -1;
        } else if (exit_status == 0)
            daemon_log(1, "child %d exited with success\n",
                       pid);
        else
            daemon_log(1, "child %d exited with status %d\n",
                       pid, exit_status);

        list_remove(&child->siblings);
    }

    if (list_empty(&instance->children))
        event_del(&instance->child_event);

    pool_commit();
}

pid_t
create_child(struct instance *instance)
{
    pid_t pid;

    pid = fork();
    if (pid < 0) {
        daemon_log(1, "fork() failed: %s\n", strerror(errno));
    } else if (pid == 0) {
        deinit_signals(instance);

        event_del(&instance->child_event);

        if (!list_empty(&instance->children)) {
            event_del(&instance->child_event);
            do
                list_remove(instance->children.next);
            while (!list_empty(&instance->children));
        }

        if (instance->listener != NULL)
            listener_event_del(instance->listener);

        while (!list_empty(&instance->connections))
            remove_connection((struct client_connection*)instance->connections.next);

        session_manager_deinit();

        event_base_free(instance->event_base);
        instance->event_base = event_init();

        init_signals(instance);

        session_manager_init(instance->pool);

        if (instance->listener != NULL)
            listener_event_add(instance->listener);
    } else {
        struct child *child;

        if (list_empty(&instance->children)) {
            event_set(&instance->child_event, SIGCHLD, EV_SIGNAL|EV_PERSIST,
                      child_event_callback, instance);
            event_add(&instance->child_event, NULL);
        }

        /* XXX leak */
        child = p_calloc(instance->pool, sizeof(*child));
        child->pid = pid;

        list_add(&child->siblings, &instance->children);
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
