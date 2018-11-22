/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Worker.hxx"
#include "Connection.hxx"
#include "Control.hxx"
#include "Instance.hxx"
#include "http_server/http_server.hxx"
#include "session/Manager.hxx"
#include "spawn/Client.hxx"
#include "event/net/ServerSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/Logger.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/PrintException.hxx"

#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

void
BpInstance::RespawnWorkerCallback()
{
    if (should_exit || workers.size() >= config.num_workers)
        return;

    LogConcat(2, "worker", "respawning worker");

    try {
        pid_t pid = SpawnWorker();
        if (pid != 0)
            ScheduleSpawnWorker();
    } catch (const std::exception &e) {
        PrintException(e);
    }
}

void
BpInstance::ScheduleSpawnWorker()
{
    if (!should_exit && workers.size() < config.num_workers &&
        !spawn_worker_event.IsPending())
        spawn_worker_event.Schedule(std::chrono::seconds(1));
}

void
BpWorker::OnChildProcessExit(int status) noexcept
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

        LogConcat(1, "worker", "abandoning shared memory, preparing to kill and respawn all workers");

        session_manager_abandon();

        session_manager_init(instance.event_loop,
                             instance.config.session_idle_timeout,
                             instance.config.cluster_size,
                             instance.config.cluster_node);

        instance.KillAllWorkers();
    }

    instance.ScheduleSpawnWorker();

    delete this;
}

void
BpInstance::InitWorker()
{
    ForkCow(false);
    ScheduleCompress();
}

pid_t
BpInstance::SpawnWorker()
{
    assert(!crash_in_unsafe());
    assert(connections.empty());

    auto spawn_socket = spawn->Connect();

    UniqueSocketDescriptor distribute_socket;
    if (!config.control_listen.empty() && config.num_workers != 1)
        distribute_socket = global_control_handler_add_fd(this);

    struct crash crash;
    crash_init(&crash);

    pid_t pid = fork();
    if (pid < 0) {
        LogConcat(1, "worker", "fork() failed: ", strerror(errno));

        crash_deinit(&crash);
    } else if (pid == 0) {
        event_loop.Reinit();

        crash_deinit(&global_crash);
        global_crash = crash;

        InitWorker();

        spawn->ReplaceSocket(std::move(spawn_socket));

        if (distribute_socket.IsDefined())
            global_control_handler_set_fd(this, std::move(distribute_socket));
        else if (config.num_workers == 1)
            /* in single-worker mode with watchdog master process, let
               only the one worker handle control commands */
            global_control_handler_enable(*this);

        /* open a new implicit control channel in the new worker
           process */
        local_control_handler_open(this);

        config.num_workers = 0;

        workers.clear_and_dispose(DeleteDisposer());

        child_process_registry.Clear();
        session_manager_event_del();

        session_manager_init(event_loop,
                             config.session_idle_timeout,
                             config.cluster_size,
                             config.cluster_node);

        EnableListeners();
    } else {
        event_loop.Reinit();

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
            LogConcat(1, "worker", "failed to kill worker ",
                      (int)worker.pid, ": ", strerror(errno));
    }
}
