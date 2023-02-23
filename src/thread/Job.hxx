// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/IntrusiveList.hxx"

#include <cstdint>

/**
 * A job that shall be executed in a worker thread.
 */
class ThreadJob : public IntrusiveListHook<IntrusiveHookMode::NORMAL> {
public:
	enum class State : uint8_t {
		/**
		 * The job is not in any queue.
		 */
		INITIAL,

		/**
		 * The job has been added to the queue, but is not being worked on
		 * yet.
		 */
		WAITING,

		/**
		 * The job is being performed via Run().
		 */
		BUSY,

		/**
		 * The job has finished, but the Done() method has not been
		 * invoked yet.
		 */
		DONE,
	};

	State state = State::INITIAL;

	/**
	 * Shall this job be enqueued again instead of invoking its Done()
	 * method?
	 */
	bool again = false;

	/**
	 * Is this job currently idle, i.e. not being worked on by a
	 * worker thread?  This method may be called only from the main
	 * thread.  A "true" return value guarantees that no worker thread
	 * is and will be working on it, and its internal data structures
	 * may be accessed without mutex protection.  Use this method with
	 * caution.
	 */
	bool IsIdle() const noexcept {
		return state == State::INITIAL;
	}

	/**
	 * Invoked in a worker thread.
	 */
	virtual void Run() noexcept = 0;

	/**
	 * Invoked in the main thread after Run() has finished.
	 */
	virtual void Done() noexcept = 0;
};
