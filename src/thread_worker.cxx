/*
 * A thread that performs queued work.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "thread_worker.hxx"
#include "thread_queue.hxx"
#include "thread_job.hxx"
#include "ssl/ssl_init.hxx"

static void *
thread_worker_run(void *ctx)
{
    /* reduce glibc's thread cancellation overhead */
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);

    struct thread_worker &w = *(struct thread_worker *)ctx;
    ThreadQueue &q = *w.queue;

    ThreadJob *job;
    while ((job = thread_queue_wait(q)) != nullptr) {
        job->Run();
        thread_queue_done(q, *job);
    }

    ssl_thread_deinit();

    return nullptr;
}

bool
thread_worker_create(struct thread_worker &w, ThreadQueue &q)
{
    w.queue = &q;

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    /* 64 kB stack ought to be enough */
    pthread_attr_setstacksize(&attr, 65536);

    bool success = pthread_create(&w.thread, &attr,
                                  thread_worker_run, &w) == 0;
    pthread_attr_destroy(&attr);
    return success;
}
