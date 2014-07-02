/*
 * A queue that manages work for worker threads.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "thread_queue.hxx"
#include "thread_job.hxx"
#include "notify.hxx"

#include <glib.h>

#include <mutex>
#include <condition_variable>

#include <assert.h>
#include <stdio.h>

class ThreadQueue {
public:
    std::mutex mutex;
    std::condition_variable cond;

    bool alive;

    /**
     * Was the #wakeup_event triggered?  This avoids duplicate events.
     */
    bool pending;

    struct list_head waiting, busy, done;

    Notify *notify;
};

static void
thread_queue_wakeup_callback(void *ctx)
{
    ThreadQueue *q = (ThreadQueue *)ctx;
    q->mutex.lock();

    q->pending = false;

    while (!list_empty(&q->done)) {
        ThreadJob *job = (ThreadJob *)q->done.next;
        assert(job->state == ThreadJob::State::DONE);

        list_remove(&job->siblings);

        if (job->again) {
            /* schedule this job again */
            job->state = ThreadJob::State::WAITING;
            job->again = false;
            list_add(&job->siblings, &q->waiting);
            q->cond.notify_one();
        } else {
            job->state = ThreadJob::State::INITIAL;
            q->mutex.unlock();
            job->done(job);
            q->mutex.lock();
        }
    }

    q->mutex.unlock();

    if (list_empty(&q->waiting) && list_empty(&q->busy) &&
        list_empty(&q->done))
        notify_disable(q->notify);
}

ThreadQueue *
thread_queue_new()
{
    auto q = new ThreadQueue();

    q->alive = true;
    q->pending = false;

    list_init(&q->waiting);
    list_init(&q->busy);
    list_init(&q->done);

    GError *error = nullptr;
    q->notify = notify_new(thread_queue_wakeup_callback, q, &error);
    if (q->notify == nullptr)
        fprintf(stderr, "%s\n", error->message);

    return q;
}

void
thread_queue_stop(ThreadQueue *q)
{
    std::unique_lock<std::mutex> lock(q->mutex);
    q->alive = false;
    q->cond.notify_all();
}

void
thread_queue_free(ThreadQueue *q)
{
    assert(!q->alive);

    notify_free(q->notify);

    delete q;
}

void
thread_queue_add(ThreadQueue *q, ThreadJob *job)
{
    q->mutex.lock();
    assert(q->alive);

    if (job->state == ThreadJob::State::INITIAL) {
        job->state = ThreadJob::State::WAITING;
        job->again = false;
        list_add(&job->siblings, &q->waiting);
        q->cond.notify_one();
    } else if (job->state != ThreadJob::State::WAITING) {
        job->again = true;
    }

    q->mutex.unlock();

    notify_enable(q->notify);
}

ThreadJob *
thread_queue_wait(ThreadQueue *q)
{
    std::unique_lock<std::mutex> lock(q->mutex);

    /* queue is empty, wait for a new job to be added */
    q->cond.wait(lock, [q](){ return !q->alive || !list_empty(&q->waiting); });

    ThreadJob *job = nullptr;
    if (q->alive && !list_empty(&q->waiting)) {
        job = (ThreadJob *)q->waiting.next;
        assert(job->state == ThreadJob::State::WAITING);

        job->state = ThreadJob::State::BUSY;
        list_remove(&job->siblings);
        list_add(&job->siblings, &q->busy);
    }

    return job;
}

void
thread_queue_done(ThreadQueue *q, ThreadJob *job)
{
    assert(job->state == ThreadJob::State::BUSY);

    q->mutex.lock();

    job->state = ThreadJob::State::DONE;
    list_remove(&job->siblings);
    list_add(&job->siblings, &q->done);

    q->pending = true;

    q->mutex.unlock();

    notify_signal(q->notify);
}

bool
thread_queue_cancel(ThreadQueue *q, ThreadJob *job)
{
    std::unique_lock<std::mutex> lock(q->mutex);

    switch (job->state) {
    case ThreadJob::State::INITIAL:
        /* already idle */
        return true;

    case ThreadJob::State::WAITING:
        /* cancel it */
        list_remove(&job->siblings);
        job->state = ThreadJob::State::INITIAL;
        return true;

    case ThreadJob::State::BUSY:
        /* no chance */
        return false;

    case ThreadJob::State::DONE:
        /* TODO: the callback hasn't been invoked yet - do that now?
           anyway, with this pending state, we can't return success */
        return false;
    }
}
