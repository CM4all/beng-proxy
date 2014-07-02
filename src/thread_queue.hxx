/*
 * A queue that manages work for worker threads.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_THREAD_QUEUE_HXX
#define BENG_PROXY_THREAD_QUEUE_HXX

struct thread_job;
struct pool;

struct thread_queue *
thread_queue_new(struct pool *pool);

/**
 * Cancel all thread_queue_wait() calls and refuse all further calls.
 * This is used to initiate shutdown of all threads connected to this
 * queue.
 */
void
thread_queue_stop(struct thread_queue *q);

void
thread_queue_free(struct thread_queue *q);

/**
 * Enqueue a job, and wake up an idle thread (if there is any).
 */
void
thread_queue_add(struct thread_queue *q, struct thread_job *job);

/**
 * Dequeue an existing job or wait for a new job, and reserve it.
 *
 * @return NULL if thread_queue_stop() has been called
 */
struct thread_job *
thread_queue_wait(struct thread_queue *q);

/**
 * Mark the specified job (returned by thread_queue_wait()) as "done".
 */
void
thread_queue_done(struct thread_queue *q, struct thread_job *job);

/**
 * Cancel a job that has been queued.
 *
 * @return true if the job is now canceled, false if the job is
 * currently being processed
 */
bool
thread_queue_cancel(struct thread_queue *q, struct thread_job *job);

#endif
