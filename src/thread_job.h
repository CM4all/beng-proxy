/*
 * A job that shall be executed in a worker thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_THREAD_JOB_H
#define BENG_PROXY_THREAD_JOB_H

#include <inline/list.h>

#include <stdbool.h>

enum thread_job_state {
    /**
     * The job is not in any queue.
     */
    THREAD_JOB_NULL,

    /**
     * The job has been added to the queue, but is not being worked on
     * yet.
     */
    THREAD_JOB_WAITING,

    /**
     * The job is being performed via run().
     */
    THREAD_JOB_BUSY,

    /**
     * The job has finished, but the done() method has not been
     * invoked yet.
     */
    THREAD_JOB_DONE,
};

struct thread_job {
    struct list_head siblings;

    enum thread_job_state state;

    /**
     * Shall this job be enqueued again instead of invoking its done()
     * method?
     */
    bool again;

    void (*run)(struct thread_job *job);
    void (*done)(struct thread_job *job);
};

static inline void
thread_job_init(struct thread_job *job,
                void (*run)(struct thread_job *job),
                void (*done)(struct thread_job *job))
{
    job->state = THREAD_JOB_NULL;
    job->again = false;
    job->run = run;
    job->done = done;
}

#endif
