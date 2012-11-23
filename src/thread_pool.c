/*
 * A queue that manages work for worker threads.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "thread_pool.h"
#include "thread_queue.h"
#include "thread_worker.h"

#include <daemon/log.h>

#include <glib.h>

#include <assert.h>
#include <stdlib.h>

struct thread_queue *global_thread_queue;
static struct thread_worker worker_threads[8];

void
thread_pool_init(struct pool *pool)
{
    global_thread_queue = thread_queue_new(pool);
}

void
thread_pool_start(void)
{
    assert(global_thread_queue != NULL);

    for (unsigned i = 0; i < G_N_ELEMENTS(worker_threads); ++i) {
        if (!thread_worker_create(&worker_threads[i], global_thread_queue)) {
            daemon_log(1, "Failed to launch worker thread\n");
            exit(EXIT_FAILURE);
        }
    }
}

void
thread_pool_stop(void)
{
    assert(global_thread_queue != NULL);

    thread_queue_stop(global_thread_queue);
}

void
thread_pool_join(void)
{
    assert(global_thread_queue != NULL);

    for (unsigned i = 0; i < G_N_ELEMENTS(worker_threads); ++i)
        thread_worker_join(&worker_threads[i]);
}

void
thread_pool_deinit(void)
{
    assert(global_thread_queue != NULL);

    thread_queue_free(global_thread_queue);
    global_thread_queue = NULL;
}
