/*
 * A thread that performs queued work.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "thread_worker.hxx"
#include "thread_queue.h"
#include "thread_job.h"

static void *
thread_worker_run(void *ctx)
{
    struct thread_worker *w = (struct thread_worker *)ctx;
    struct thread_queue *q = w->queue;

    struct thread_job *job;
    while ((job = thread_queue_wait(q)) != nullptr) {
        job->run(job);
        thread_queue_done(q, job);
    }

    return nullptr;
}

bool
thread_worker_create(struct thread_worker *w, struct thread_queue *q)
{
    w->queue = q;

    return pthread_create(&w->thread, nullptr, thread_worker_run, w) == 0;
}
