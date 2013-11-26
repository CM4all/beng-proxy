/*
 * A queue that manages work for worker threads.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "thread_queue.h"
#include "thread_job.h"
#include "pool.h"
#include "notify.h"

#include <pthread.h>
#include <assert.h>

struct thread_queue {
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    bool alive;

    /**
     * Was the #wakeup_event triggered?  This avoids duplicate events.
     */
    bool pending;

    struct list_head waiting, busy, done;

    struct notify *notify;
};

static void
thread_queue_wakeup_callback(void *ctx)
{
    struct thread_queue *q = ctx;
    pthread_mutex_lock(&q->mutex);

    q->pending = false;

    while (!list_empty(&q->done)) {
        struct thread_job *job = (struct thread_job *)q->done.next;
        assert(job->state == THREAD_JOB_DONE);

        list_remove(&job->siblings);

        if (job->again) {
            /* schedule this job again */
            job->state = THREAD_JOB_WAITING;
            job->again = false;
            list_add(&job->siblings, &q->waiting);
            pthread_cond_signal(&q->cond);
        } else {
            job->state = THREAD_JOB_NULL;
            pthread_mutex_unlock(&q->mutex);
            job->done(job);
            pthread_mutex_lock(&q->mutex);
        }
    }

    pthread_mutex_unlock(&q->mutex);

    if (list_empty(&q->waiting) && list_empty(&q->busy) &&
        list_empty(&q->done))
        notify_disable(q->notify);
}

struct thread_queue *
thread_queue_new(struct pool *pool)
{
    struct thread_queue *q = p_malloc(pool, sizeof(*q));

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);

    q->alive = true;
    q->pending = false;

    list_init(&q->waiting);
    list_init(&q->busy);
    list_init(&q->done);

    GError *error = NULL;
    q->notify = notify_new(pool, thread_queue_wakeup_callback, q, &error);
    if (q->notify == NULL)
        g_printerr("%s\n", error->message);

    return q;
}

void
thread_queue_stop(struct thread_queue *q)
{
    pthread_mutex_lock(&q->mutex);
    q->alive = false;
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

void
thread_queue_free(struct thread_queue *q)
{
    assert(!q->alive);

    notify_free(q->notify);

    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

void
thread_queue_add(struct thread_queue *q, struct thread_job *job)
{
    pthread_mutex_lock(&q->mutex);
    assert(q->alive);

    if (job->state == THREAD_JOB_NULL) {
        job->state = THREAD_JOB_WAITING;
        job->again = false;
        list_add(&job->siblings, &q->waiting);
        pthread_cond_signal(&q->cond);
    } else if (job->state != THREAD_JOB_WAITING) {
        job->again = true;
    }

    pthread_mutex_unlock(&q->mutex);

    notify_enable(q->notify);
}

struct thread_job *
thread_queue_wait(struct thread_queue *q)
{
    pthread_mutex_lock(&q->mutex);

    while (q->alive && list_empty(&q->waiting))
        /* queue is empty, wait for a new job to be added */
        pthread_cond_wait(&q->cond, &q->mutex);

    struct thread_job *job = NULL;
    if (q->alive && !list_empty(&q->waiting)) {
        job = (struct thread_job *)q->waiting.next;
        assert(job->state == THREAD_JOB_WAITING);

        job->state = THREAD_JOB_BUSY;
        list_remove(&job->siblings);
        list_add(&job->siblings, &q->busy);
    }

    pthread_mutex_unlock(&q->mutex);

    return job;
}

void
thread_queue_done(struct thread_queue *q, struct thread_job *job)
{
    assert(job->state == THREAD_JOB_BUSY);

    pthread_mutex_lock(&q->mutex);

    job->state = THREAD_JOB_DONE;
    list_remove(&job->siblings);
    list_add(&job->siblings, &q->done);

    q->pending = true;

    pthread_mutex_unlock(&q->mutex);

    notify_signal(q->notify);
}

bool
thread_queue_cancel(struct thread_queue *q, struct thread_job *job)
{
    pthread_mutex_lock(&q->mutex);

    bool result = false;
    switch (job->state) {
    case THREAD_JOB_NULL:
        /* already idle */
        result = true;
        break;

    case THREAD_JOB_WAITING:
        /* cancel it */
        list_remove(&job->siblings);
        job->state = THREAD_JOB_NULL;
        result = true;
        break;

    case THREAD_JOB_BUSY:
        /* no chance */
        break;

    case THREAD_JOB_DONE:
        /* TODO: the callback hasn't been invoked yet - do that now?
           anyway, with this pending state, we can't return success */
        break;
    }

    pthread_mutex_unlock(&q->mutex);
    return result;
}
