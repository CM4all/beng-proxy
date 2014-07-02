/*
 * A thread that performs queued work.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_THREAD_WORKER_H
#define BENG_PROXY_THREAD_WORKER_H

#include <inline/list.h>

#include <pthread.h>
#include <stdbool.h>

struct thread_worker {
    pthread_t thread;

    struct thread_queue *queue;
};

#ifdef __cplusplus
extern "C" {
#endif

bool
thread_worker_create(struct thread_worker *w, struct thread_queue *q);

/**
 * Wait for the thread to exit.  You must call thread_queue_stop()
 * prior to this function.
 */
static inline void
thread_worker_join(struct thread_worker *w)
{
    pthread_join(w->thread, NULL);
}

#ifdef __cplusplus
}
#endif

#endif
