/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "util/Cancellable.hxx"
#include "util/IntrusiveList.hxx"

/**
 * A job running in the background, which shall be aborted when
 * beng-proxy is shut down.  The job holds a reference to an
 * #Cancellable object, which may be used to stop it.
 */
struct BackgroundJob : IntrusiveListHook {
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
