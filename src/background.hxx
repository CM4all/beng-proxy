/*
 * Managing background jobs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BACKGROUND_HXX
#define BENG_PROXY_BACKGROUND_HXX

#include "async.h"

#include <inline/list.h>

/**
 * A job running in the background, which shall be aborted when
 * beng-proxy is shut down.  The job holds a reference to an
 * #async_operation object, which may be used to stop it.
 */
struct background_job {
    struct list_head siblings;

    struct async_operation_ref async_ref;
};

static inline struct background_job *
list_head_to_background_job(struct list_head *head)
{
    const void *p = ((char *)head) - offsetof(struct background_job, siblings);
    return (struct background_job *)p;
}

/**
 * A container for background jobs.
 */
class BackgroundManager {
    struct list_head jobs;

public:
    BackgroundManager() {
        list_init(&jobs);
    }

    /**
     * Register a job to the manager.
     */
    void Add(struct background_job &job) {
        list_add(&job.siblings, &jobs);
    }

    /**
     * Add a background job to the manager, and return its
     * #async_operation_ref.  This is a convenience function.
     */
    struct async_operation_ref *Add2(struct background_job &job) {
        Add(job);
        return &job.async_ref;
    }

    /**
     * Leave the job registered in the manager, and reuse its
     * #async_operation_ref for another job iteration.
     */
    struct async_operation_ref *Reuse(struct background_job &job) {
        return &job.async_ref;
    }

    /**
     * Unregister a job from the manager.
     */
    void Remove(struct background_job &job) {
        list_remove(&job.siblings);
    }

    /**
     * Abort the job and unregister it from the manager.  This function
     * should not be called, it is used internally.
     */
    void AbortInternal(struct background_job &job) {
        Remove(job);
        async_abort(&job.async_ref);
    }

    /**
     * Abort all background jobs in the manager.  This is called on
     * shutdown.
     */
    void AbortAll() {
        while (!list_empty(&jobs)) {
            struct background_job *job =
                list_head_to_background_job(jobs.next);

            AbortInternal(*job);
        }
    }
};

class LinkedBackgroundJob : public background_job {
    BackgroundManager &manager;

public:
    LinkedBackgroundJob(BackgroundManager &_manager):manager(_manager) {}

    void Remove() {
        manager.Remove(*this);
    }
};

#endif
