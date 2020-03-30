/*
 * Copyright 2007-2020 CM4all GmbH
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

/*
 * A queue that manages work for worker threads.
 */

#pragma once

#include "Notify.hxx"

#include <boost/intrusive/list.hpp>

#include <mutex>
#include <condition_variable>

class EventLoop;
class ThreadQueue;
class ThreadJob;

class ThreadQueue {
	std::mutex mutex;
	std::condition_variable cond;

	bool alive = true;

	/**
	 * Was the #wakeup_event triggered?  This avoids duplicate events.
	 */
	bool pending = false;

	typedef boost::intrusive::list<ThreadJob,
				       boost::intrusive::base_hook<boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>>,
				       boost::intrusive::constant_time_size<false>> JobList;

	JobList waiting, busy, done;

	Notify notify;

public:
	explicit ThreadQueue(EventLoop &event_loop) noexcept;
	~ThreadQueue() noexcept;

	bool IsEmpty() const noexcept {
		return waiting.empty() && busy.empty() && done.empty();
	}

	/**
	 * Cancel all Wait() calls and refuse all further calls.
	 * This is used to initiate shutdown of all threads connected to this
	 * queue.
	 */
	void Stop() noexcept;

	/**
	 * Enqueue a job, and wake up an idle thread (if there is any).
	 */
	void Add(ThreadJob &job) noexcept;

	/**
	 * Dequeue an existing job or wait for a new job, and reserve it.
	 *
	 * @return NULL if thread_queue_stop() has been called
	 */
	ThreadJob *Wait() noexcept;

	/**
	 * Mark the specified job (returned by thread_queue_wait()) as "done".
	 */
	void Done(ThreadJob &job) noexcept;

	/**
	 * Cancel a job that has been queued.
	 *
	 * @return true if the job is now canceled, false if the job is
	 * currently being processed
	 */
	bool Cancel(ThreadJob &job) noexcept;

private:
	void WakeupCallback() noexcept;
};
