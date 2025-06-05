// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/Cancellable.hxx"
#include "util/IntrusiveList.hxx"

/**
 * A job running in the background, which shall be aborted when
 * beng-proxy is shut down.  The job holds a reference to an
 * #Cancellable object, which may be used to stop it.
 */
struct BackgroundJob : IntrusiveListHook<IntrusiveHookMode::NORMAL> {
	CancellablePointer cancel_ptr;
};

/**
 * A container for background jobs.
 */
class BackgroundManager {
	IntrusiveList<BackgroundJob> jobs;

public:
	/**
	 * Register a job to the manager.
	 */
	void Add(BackgroundJob &job) noexcept {
		jobs.push_front(job);
	}

	/**
	 * Add a background job to the manager, and return its
	 * #CancellablePointer.  This is a convenience function.
	 */
	CancellablePointer &Add2(BackgroundJob &job) noexcept {
		Add(job);
		return job.cancel_ptr;
	}

	/**
	 * Leave the job registered in the manager, and reuse its
	 * #CancellablePointer for another job iteration.
	 */
	CancellablePointer &Reuse(BackgroundJob &job) noexcept {
		return job.cancel_ptr;
	}

	/**
	 * Unregister a job from the manager.
	 */
	void Remove(BackgroundJob &job) noexcept {
		job.unlink();
	}

	/**
	 * Abort all background jobs in the manager.  This is called on
	 * shutdown.
	 */
	void AbortAll() noexcept {
		jobs.clear_and_dispose([](BackgroundJob *job){
			job->cancel_ptr.Cancel();
		});
	}
};
