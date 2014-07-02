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

    typedef boost::intrusive::list<ThreadJob,
                                   boost::intrusive::constant_time_size<false>> JobList;

    JobList waiting, busy, done;

    Notify *notify;
};

static void
thread_queue_wakeup_callback(void *ctx)
{
    ThreadQueue *q = (ThreadQueue *)ctx;
    q->mutex.lock();

    q->pending = false;

    for (auto i = q->done.begin(), end = q->done.end(); i != end;) {
        ThreadJob *job = &*i;
        assert(job->state == ThreadJob::State::DONE);

        i = q->done.erase(i);

        if (job->again) {
            /* schedule this job again */
            job->state = ThreadJob::State::WAITING;
            job->again = false;
            q->waiting.push_back(*job);
            q->cond.notify_one();
        } else {
            job->state = ThreadJob::State::INITIAL;
            q->mutex.unlock();
            job->done(job);
            q->mutex.lock();
        }
    }

    q->mutex.unlock();

    if (q->waiting.empty() && q->busy.empty() && q->done.empty())
        notify_disable(q->notify);
}

ThreadQueue *
thread_queue_new()
{
    auto q = new ThreadQueue();

    q->alive = true;
    q->pending = false;

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
        q->waiting.push_back(*job);
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

    while (true) {
        if (!q->alive)
            return nullptr;

        auto i = q->waiting.begin();
        if (i != q->waiting.end()) {
            auto &job = *i;
            assert(job.state == ThreadJob::State::WAITING);

            job.state = ThreadJob::State::BUSY;
            q->waiting.erase(i);
            q->busy.push_back(job);
            return &job;
        }

        /* queue is empty, wait for a new job to be added */
        q->cond.wait(lock);
    }
}

void
thread_queue_done(ThreadQueue *q, ThreadJob *job)
{
    assert(job->state == ThreadJob::State::BUSY);

    q->mutex.lock();

    job->state = ThreadJob::State::DONE;
    q->busy.erase(q->busy.iterator_to(*job));
    q->done.push_back(*job);

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
        q->waiting.erase(q->waiting.iterator_to(*job));
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
