/*
 * Child process management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_worker.hxx"
#include "http_server.hxx"
#include "pool.hxx"
#include "bp_instance.hxx"
#include "bp_connection.hxx"
#include "session_manager.hxx"
#include "child_manager.hxx"
#include "bp_control.hxx"
#include "net/ServerSocket.hxx"

#include <daemon/log.h>

#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static void
schedule_respawn(struct instance *instance);

static void
respawn_event_callback(int fd gcc_unused, short event gcc_unused,
                       void *ctx)
{
    struct instance *instance = (struct instance*)ctx;

    if (instance->should_exit ||
        instance->num_workers >= instance->config.num_workers)
        return;

    daemon_log(2, "respawning child\n");

    pid_t pid = worker_new(instance);
    if (pid != 0)
        schedule_respawn(instance);
}

static void
schedule_respawn(struct instance *instance)
{
    if (!instance->should_exit &&
        instance->num_workers < instance->config.num_workers &&
        evtimer_pending(&instance->respawn_event, nullptr) == 0) {
        static const struct timeval tv = {
            .tv_sec = 1,
            .tv_usec = 0,
        };

        evtimer_set(&instance->respawn_event, respawn_event_callback, instance);
        evtimer_add(&instance->respawn_event, &tv);
    }
}

static void
worker_remove(struct instance *instance, struct worker *worker)
{
    list_remove(&worker->siblings);

    assert(instance->num_workers > 0);
    --instance->num_workers;
}

static void
worker_free(struct instance *instance, struct worker *worker)
{
    crash_deinit(&worker->crash);
    p_free(instance->pool, worker);
}

/**
 * Remove and free the worker.
 */
static void
worker_dispose(struct instance *instance, struct worker *worker)
{
    worker_remove(instance, worker);
    worker_free(instance, worker);
}

static void
worker_child_callback(int status, void *ctx)
{
    worker &worker = *(struct worker *)ctx;
    struct instance *instance = worker.instance;
    int exit_status = WEXITSTATUS(status);

    if (WIFSIGNALED(status)) {
        int level = 1;
        if (!WCOREDUMP(status) && WTERMSIG(status) == SIGTERM)
            level = 3;

        daemon_log(level, "worker %d died from signal %d%s\n",
                   worker.pid, WTERMSIG(status),
                   WCOREDUMP(status) ? " (core dumped)" : "");
    } else if (exit_status == 0)
        daemon_log(1, "worker %d exited with success\n",
                   worker.pid);
    else
        daemon_log(1, "worker %d exited with status %d\n",
                   worker.pid, exit_status);

    const bool safe = crash_is_safe(&worker.crash);
    worker_dispose(instance, &worker);

    if (WIFSIGNALED(status) && !instance->should_exit && !safe) {
        /* a worker has died due to a signal - this is dangerous for
           all other processes (including us), because the worker may
           have corrupted shared memory.  Our only hope to recover is
           to immediately free all shared memory, kill all workers
           still using it, and spawn new workers with fresh shared
           memory. */

        daemon_log(1, "abandoning shared memory, preparing to kill and respawn all workers\n");

        session_manager_abandon();

        if (!session_manager_init(instance->config.session_idle_timeout,
                                  instance->config.cluster_size,
                                  instance->config.cluster_node)) {
            daemon_log(1, "session_manager_init() failed\n");
            _exit(2);
        }

        worker_killall(instance);
    }

    schedule_respawn(instance);
}

pid_t
worker_new(struct instance *instance)
{
    assert(!crash_in_unsafe());

    deinit_signals(instance);
    children_event_del();

    int distribute_socket = -1;
    if (instance->config.control_listen != nullptr) {
        distribute_socket = global_control_handler_add_fd(instance);
        if (distribute_socket < 0) {
            daemon_log(1, "udp_distribute_add() failed: %s\n",
                       strerror(errno));
            return -1;
        }
    }

    struct crash crash;
    if (!crash_init(&crash))
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        daemon_log(1, "fork() failed: %s\n", strerror(errno));

        init_signals(instance);
        children_event_add();

        if (distribute_socket >= 0)
            close(distribute_socket);

        crash_deinit(&crash);
    } else if (pid == 0) {
        event_reinit(instance->event_base);

        crash_deinit(&global_crash);
        global_crash = crash;

        instance->ForkCow(false);

        if (distribute_socket >= 0)
            global_control_handler_set_fd(instance, distribute_socket);

        /* open a new implicit control channel in the new worker
           process */
        local_control_handler_open(instance);

        instance->config.num_workers = 0;

        list_init(&instance->workers);
        instance->num_workers = 0;

        all_listeners_event_del(instance);

        while (!list_empty(&instance->connections))
            close_connection((struct client_connection*)instance->connections.next);

        init_signals(instance);
        children_init();

        session_manager_event_del();

        gcc_unused
        bool ret = session_manager_init(instance->config.session_idle_timeout,
                                        instance->config.cluster_size,
                                        instance->config.cluster_node);
        assert(ret);

        all_listeners_event_add(instance);
    } else {
        if (distribute_socket >= 0)
            close(distribute_socket);

        event_reinit(instance->event_base);

        worker *worker = NewFromPool<struct worker>(*instance->pool);
        worker->instance = instance;
        worker->pid = pid;
        worker->crash = crash;

        list_add(&worker->siblings, &instance->workers);
        ++instance->num_workers;

        init_signals(instance);
        children_event_add();

        child_register(pid, "worker", worker_child_callback, worker);
    }

    return pid;
}

void
worker_killall(struct instance *instance)
{
    for (struct worker *worker = (struct worker*)instance->workers.next;
         worker != (struct worker*)&instance->workers;
         worker = (struct worker*)worker->siblings.next) {
        if (kill(worker->pid, SIGTERM) < 0)
            daemon_log(1, "failed to kill worker %d: %s\n",
                       (int)worker->pid, strerror(errno));
    }
}
