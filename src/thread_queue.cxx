/*
 * A queue that manages work for worker threads.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "thread_queue.hxx"
#include "thread_job.hxx"
#include "notify.hxx"

#include <inline/compiler.h>

#include <mutex>
#include <condition_variable>

#include <assert.h>

class ThreadQueue {
public:
    std::mutex mutex;
    std::condition_variable cond;

    bool alive = true;

    /**
     * Was the #wakeup_event triggered?  This avoids duplicate events.
     */
    bool pending = false;

    typedef boost::intrusive::list<ThreadJob,
                                   boost::intrusive::constant_time_size<false>> JobList;

    JobList waiting, busy, done;

    Notify *const notify;

    ThreadQueue()
        :notify(notify_new(BIND_THIS_METHOD(WakeupCallback))) {}

    ~ThreadQueue() {
        assert(!alive);

        notify_free(notify);
    }

    bool IsEmpty() const {
        return waiting.empty() && busy.empty() && done.empty();
    }

    void WakeupCallback();
};

void
ThreadQueue::WakeupCallback()
{
    mutex.lock();

    pending = false;

    for (auto i = done.begin(), end = done.end(); i != end;) {
        ThreadJob *job = &*i;
        assert(job->state == ThreadJob::State::DONE);

        i = done.erase(i);

        if (job->again) {
            /* schedule this job again */
            job->state = ThreadJob::State::WAITING;
            job->again = false;
            waiting.push_back(*job);
            cond.notify_one();
        } else {
            job->state = ThreadJob::State::INITIAL;
            mutex.unlock();
            job->Done();
            mutex.lock();
        }
    }

    const bool empty = IsEmpty();

    mutex.unlock();

    if (empty)
        notify_disable(notify);
}

ThreadQueue *
thread_queue_new()
{
    return new ThreadQueue();
}

void
thread_queue_stop(ThreadQueue &q)
{
    std::unique_lock<std::mutex> lock(q.mutex);
    q.alive = false;
    q.cond.notify_all();
}

void
thread_queue_free(ThreadQueue *q)
{
    delete q;
}

void
thread_queue_add(ThreadQueue &q, ThreadJob &job)
{
    q.mutex.lock();
    assert(q.alive);

    if (job.state == ThreadJob::State::INITIAL) {
        job.state = ThreadJob::State::WAITING;
        job.again = false;
        q.waiting.push_back(job);
        q.cond.notify_one();
    } else if (job.state != ThreadJob::State::WAITING) {
        job.again = true;
    }

    q.mutex.unlock();

    notify_enable(q.notify);
}

ThreadJob *
thread_queue_wait(ThreadQueue &q)
{
    std::unique_lock<std::mutex> lock(q.mutex);

    while (true) {
        if (!q.alive)
            return nullptr;

        auto i = q.waiting.begin();
        if (i != q.waiting.end()) {
            auto &job = *i;
            assert(job.state == ThreadJob::State::WAITING);

            job.state = ThreadJob::State::BUSY;
            q.waiting.erase(i);
            q.busy.push_back(job);
            return &job;
        }

        /* queue is empty, wait for a new job to be added */
        q.cond.wait(lock);
    }
}

void
thread_queue_done(ThreadQueue &q, ThreadJob &job)
{
    assert(job.state == ThreadJob::State::BUSY);

    q.mutex.lock();

    job.state = ThreadJob::State::DONE;
    q.busy.erase(q.busy.iterator_to(job));
    q.done.push_back(job);

    q.pending = true;

    q.mutex.unlock();

    notify_signal(q.notify);
}

bool
thread_queue_cancel(ThreadQueue &q, ThreadJob &job)
{
    std::unique_lock<std::mutex> lock(q.mutex);

    switch (job.state) {
    case ThreadJob::State::INITIAL:
        /* already idle */
        return true;

    case ThreadJob::State::WAITING:
        /* cancel it */
        q.waiting.erase(q.waiting.iterator_to(job));
        job.state = ThreadJob::State::INITIAL;
        return true;

    case ThreadJob::State::BUSY:
        /* no chance */
        return false;

    case ThreadJob::State::DONE:
        /* TODO: the callback hasn't been invoked yet - do that now?
           anyway, with this pending state, we can't return success */
        return false;
    }

    assert(false);
    gcc_unreachable();
}
