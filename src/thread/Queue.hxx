// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Notify.hxx"
#include "util/IntrusiveList.hxx"

#include <mutex>
#include <condition_variable>

class EventLoop;
class ThreadJob;

/**
 * A queue that manages work for worker threads (#ThreadWorker).
 */
class ThreadQueue {
	std::mutex mutex;
	std::condition_variable cond;

	bool alive = true;

	/**
	 * Is #notify in "volatile" mode, i.e. disable it as soon as
	 * the queue runs empty?  This mode is used during shutdown.
	 */
	bool volatile_notify = false;

	using JobList = IntrusiveList<ThreadJob>;

	JobList waiting, busy, done;

	Notify notify;

public:
	explicit ThreadQueue(EventLoop &event_loop) noexcept;
	~ThreadQueue() noexcept;

	/**
	 * If this mode is enabled, then the eventfd will be
	 * unregistered whenever the queue is empty.
	 */
	void SetVolatile() noexcept {
		volatile_notify = true;
		LockCheckDisableNotify();
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
	 * @return nullptr if Stop() has been called
	 */
	ThreadJob *Wait() noexcept;

	/**
	 * Mark the specified job (returned by Wait()) as "done".
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
	bool IsEmpty() const noexcept {
		return waiting.empty() && busy.empty() && done.empty();
	}

	void CheckDisableNotify() noexcept {
		if (volatile_notify && IsEmpty())
			notify.Disable();
	}

	void LockCheckDisableNotify() noexcept {
		const std::scoped_lock lock{mutex};
		CheckDisableNotify();
	}

	void _Add(ThreadJob &job) noexcept;

	void WakeupCallback() noexcept;
};
