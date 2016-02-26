/*
 * Child process management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_worker.hxx"
#include "http_server/http_server.hxx"
#include "bp_instance.hxx"
#include "bp_connection.hxx"
#include "session_manager.hxx"
#include "bp_control.hxx"
#include "spawn/Client.hxx"
#include "event/Duration.hxx"
#include "net/ServerSocket.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/PrintException.hxx"

#include <daemon/log.h>

#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

void
BpInstance::RespawnWorkerCallback()
{
    if (should_exit || workers.size() >= config.num_workers)
        return;

    daemon_log(2, "respawning child\n");

    pid_t pid = SpawnWorker();
    if (pid != 0)
        ScheduleSpawnWorker();
}

void
BpInstance::ScheduleSpawnWorker()
{
    if (!should_exit && workers.size() < config.num_workers &&
        !spawn_worker_event.IsPending())
        spawn_worker_event.Add(EventDuration<1>::value);
}

void
BpWorker::OnChildProcessExit(int status)
{
    const bool safe = crash_is_safe(&crash);

    instance.workers.erase(instance.workers.iterator_to(*this));

    if (WIFSIGNALED(status) && !instance.should_exit && !safe) {
        /* a worker has died due to a signal - this is dangerous for
           all other processes (including us), because the worker may
           have corrupted shared memory.  Our only hope to recover is
           to immediately free all shared memory, kill all workers
           still using it, and spawn new workers with fresh shared
           memory. */

        daemon_log(1, "abandoning shared memory, preparing to kill and respawn all workers\n");

        session_manager_abandon();

        if (!session_manager_init(instance.config.session_idle_timeout,
                                  instance.config.cluster_size,
                                  instance.config.cluster_node)) {
            daemon_log(1, "session_manager_init() failed\n");
            _exit(2);
        }

        instance.KillAllWorkers();
    }

    instance.ScheduleSpawnWorker();

    delete this;
}

void
BpInstance::InitWorker()
{
    ForkCow(false);
}

pid_t
BpInstance::SpawnWorker()
{
    assert(!crash_in_unsafe());

#ifdef USE_SPAWNER
    int spawn_fd;

    try {
        spawn_fd = spawn->Connect();
    } catch (const std::exception &e) {
        PrintException(e);
        return -1;
    }
#endif

    int distribute_socket = -1;
    if (config.control_listen != nullptr && config.num_workers != 1) {
        distribute_socket = global_control_handler_add_fd(this);
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

#ifdef USE_SPAWNER
        close(spawn_fd);
#endif

        if (distribute_socket >= 0)
            close(distribute_socket);

        crash_deinit(&crash);
    } else if (pid == 0) {
        event_base.Reinit();

        crash_deinit(&global_crash);
        global_crash = crash;

        InitWorker();

#ifdef USE_SPAWNER
        spawn->ReplaceSocket(spawn_fd);
#endif

        if (distribute_socket >= 0)
            global_control_handler_set_fd(this, distribute_socket);
        else if (config.num_workers == 1)
            /* in single-worker mode with watchdog master process, let
               only the one worker handle control commands */
            global_control_handler_enable(*this);

        /* open a new implicit control channel in the new worker
           process */
        local_control_handler_open(this);

        config.num_workers = 0;

        workers.clear_and_dispose(DeleteDisposer());

        while (!connections.empty())
            close_connection(&connections.front());

        child_process_registry.Clear();
        session_manager_event_del();

        gcc_unused
        bool ret = session_manager_init(config.session_idle_timeout,
                                        config.cluster_size,
                                        config.cluster_node);
        assert(ret);

        all_listeners_event_add(this);
    } else {
#ifdef USE_SPAWNER
        close(spawn_fd);
#endif

        if (distribute_socket >= 0)
            close(distribute_socket);

        event_base.Reinit();

        auto *worker = new BpWorker(*this, pid, crash);
        workers.push_back(*worker);

        child_process_registry.Add(pid, "worker", worker);
    }

    return pid;
}

void
BpInstance::KillAllWorkers()
{
    for (auto &worker : workers) {
        if (kill(worker.pid, SIGTERM) < 0)
            daemon_log(1, "failed to kill worker %d: %s\n",
                       (int)worker.pid, strerror(errno));
    }
}
