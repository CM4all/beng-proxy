/*
 * A job that shall be executed in a worker thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_THREAD_JOB_HXX
#define BENG_PROXY_THREAD_JOB_HXX

#include <inline/list.h>

class ThreadJob {
public:
    struct list_head siblings;

    enum class State {
        /**
         * The job is not in any queue.
         */
        INITIAL,

        /**
         * The job has been added to the queue, but is not being worked on
         * yet.
         */
        WAITING,

        /**
         * The job is being performed via run().
         */
        BUSY,

        /**
         * The job has finished, but the done() method has not been
         * invoked yet.
         */
        DONE,
    };

    State state;

    /**
     * Shall this job be enqueued again instead of invoking its done()
     * method?
     */
    bool again;

    void (*run)(ThreadJob *job);
    void (*done)(ThreadJob *job);
};

static inline void
thread_job_init(ThreadJob *job,
                void (*run)(ThreadJob *job),
                void (*done)(ThreadJob *job))
{
    job->state = ThreadJob::State::INITIAL;
    job->again = false;
    job->run = run;
    job->done = done;
}

#endif
