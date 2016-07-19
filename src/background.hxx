/*
 * Managing background jobs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BACKGROUND_HXX
#define BENG_PROXY_BACKGROUND_HXX

#include "util/Cancellable.hxx"

#include <boost/intrusive/list.hpp>

/**
 * A job running in the background, which shall be aborted when
 * beng-proxy is shut down.  The job holds a reference to an
 * #Cancellable object, which may be used to stop it.
 */
struct BackgroundJob {
    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::list_member_hook<LinkMode> SiblingsListHook;
    SiblingsListHook siblings;

    CancellablePointer cancel_ptr;
};

/**
 * A container for background jobs.
 */
class BackgroundManager {
    boost::intrusive::list<BackgroundJob,
                           boost::intrusive::member_hook<BackgroundJob,
                                                         BackgroundJob::SiblingsListHook,
                                                         &BackgroundJob::siblings>,
                           boost::intrusive::constant_time_size<false>> jobs;

public:
    /**
     * Register a job to the manager.
     */
    void Add(BackgroundJob &job) {
        jobs.push_front(job);
    }

    /**
     * Add a background job to the manager, and return its
     * #CancellablePointer.  This is a convenience function.
     */
    CancellablePointer &Add2(BackgroundJob &job) {
        Add(job);
        return job.cancel_ptr;
    }

    /**
     * Leave the job registered in the manager, and reuse its
     * #CancellablePointer for another job iteration.
     */
    CancellablePointer &Reuse(BackgroundJob &job) {
        return job.cancel_ptr;
    }

    /**
     * Unregister a job from the manager.
     */
    void Remove(BackgroundJob &job) {
        jobs.erase(jobs.iterator_to(job));
    }

    /**
     * Abort all background jobs in the manager.  This is called on
     * shutdown.
     */
    void AbortAll() {
        jobs.clear_and_dispose([this](BackgroundJob *job){
                job->cancel_ptr.Cancel();
            });
    }
};

class LinkedBackgroundJob : public BackgroundJob {
    BackgroundManager &manager;

public:
    LinkedBackgroundJob(BackgroundManager &_manager):manager(_manager) {}

    void Remove() {
        manager.Remove(*this);
    }
};

#endif
