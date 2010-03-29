/*
 * Managing background jobs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BACKGROUND_H
#define BENG_PROXY_BACKGROUND_H

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

/**
 * A container for background jobs.
 */
struct background_manager {
    struct list_head jobs;
};

/**
 * Initializer for the background manager.  The object is allocated by
 * the caller, and may be embeded in another struct.
 */
static inline void
background_manager_init(struct background_manager *mgr)
{
    list_init(&mgr->jobs);
}

/**
 * Register a job to the manager.
 */
static inline void
background_manager_add(struct background_manager *mgr,
                       struct background_job *job)
{
    list_add(&job->siblings, &mgr->jobs);
}

/**
 * Unregister a job from the manager.
 */
static inline void
background_manager_remove(struct background_job *job)
{
    list_remove(&job->siblings);
}

/**
 * Abort the job and unregister it from the manager.  This function
 * should not be called, it is used internally.
 */
static inline void
background_job_abort_internal(struct background_job *job)
{
    background_manager_remove(job);
    async_abort(&job->async_ref);
}

static inline struct background_job *
list_head_to_background_job(struct list_head *head)
{
    return (struct background_job *)(((char*)head) - offsetof(struct background_job, siblings));
}

/**
 * Abort all background jobs in the manager.  This is called on
 * shutdown.
 */
static inline void
background_manager_abort_all(struct background_manager *mgr)
{
    while (!list_empty(&mgr->jobs)) {
        struct background_job *job =
            list_head_to_background_job(mgr->jobs.next);

        background_job_abort_internal(job);
    }
}

/**
 * Add a background job to the manager, and return its
 * #async_operation_ref.  This is a convenience function.
 */
static inline struct async_operation_ref *
background_job_add(struct background_manager *mgr,
                   struct background_job *job)
{
    background_manager_add(mgr, job);
    return &job->async_ref;
}

/**
 * Leave the job registered in the manager, and reuse its
 * #async_operation_ref for another job iteration.
 */
static inline struct async_operation_ref *
background_job_reuse(struct background_job *job)
{
    return &job->async_ref;
}

#endif
