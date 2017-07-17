/*
 * A thread that performs queued work.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_THREAD_WORKER_HXX
#define BENG_PROXY_THREAD_WORKER_HXX

#include <pthread.h>

class ThreadQueue;

struct thread_worker {
    pthread_t thread;

    ThreadQueue *queue;
};

/**
 * Throws exception on error.
 */
void
thread_worker_create(struct thread_worker &w, ThreadQueue &q);

/**
 * Wait for the thread to exit.  You must call thread_queue_stop()
 * prior to this function.
 */
static inline void
thread_worker_join(struct thread_worker &w)
{
    pthread_join(w.thread, nullptr);
}

#endif
