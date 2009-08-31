/*
 * Managing background jobs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BACKGROUND_H
#define BENG_PROXY_BACKGROUND_H

#include "async.h"

#include <inline/list.h>

struct background_job {
    struct list_head siblings;

    struct async_operation_ref async_ref;
};

struct background_manager {
    struct list_head jobs;
};

static inline void
background_manager_init(struct background_manager *mgr)
{
    list_init(&mgr->jobs);
}

static inline void
background_manager_add(struct background_manager *mgr,
                       struct background_job *job)
{
    list_add(&job->siblings, &mgr->jobs);
}

static inline void
background_manager_remove(struct background_job *job)
{
    list_remove(&job->siblings);
}

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

static inline void
background_manager_abort_all(struct background_manager *mgr)
{
    while (!list_empty(&mgr->jobs)) {
        struct background_job *job =
            list_head_to_background_job(mgr->jobs.next);

        background_job_abort_internal(job);
    }
}

static inline struct async_operation_ref *
background_job_add(struct background_manager *mgr,
                   struct background_job *job)
{
    background_manager_add(mgr, job);
    return &job->async_ref;
}

static inline struct async_operation_ref *
background_job_reuse(struct background_job *job)
{
    return &job->async_ref;
}

#endif
