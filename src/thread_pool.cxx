/*
 * A queue that manages work for worker threads.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "thread_pool.hxx"
#include "thread_queue.hxx"
#include "thread_worker.hxx"

#include <daemon/log.h>

#include <array>

#include <assert.h>
#include <stdlib.h>

static ThreadQueue *global_thread_queue;
static std::array<struct thread_worker, 8> worker_threads;

static void
thread_pool_init()
{
    global_thread_queue = thread_queue_new();
}

static void
thread_pool_start(void)
{
    assert(global_thread_queue != nullptr);

    for (auto &i : worker_threads) {
        if (!thread_worker_create(i, *global_thread_queue)) {
            daemon_log(1, "Failed to launch worker thread\n");
            exit(EXIT_FAILURE);
        }
    }
}

ThreadQueue &
thread_pool_get_queue()
{
    if (global_thread_queue == nullptr) {
        /* initial call - create the queue and launch worker
           threads */
        thread_pool_init();
        thread_pool_start();
    }

    return *global_thread_queue;
}

void
thread_pool_stop(void)
{
    if (global_thread_queue == nullptr)
        return;

    thread_queue_stop(*global_thread_queue);
}

void
thread_pool_join(void)
{
    if (global_thread_queue == nullptr)
        return;

    for (auto &i : worker_threads)
        thread_worker_join(i);
}

void
thread_pool_deinit(void)
{
    if (global_thread_queue == nullptr)
        return;

    thread_queue_free(global_thread_queue);
    global_thread_queue = nullptr;
}
