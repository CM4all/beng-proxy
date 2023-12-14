// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <pthread.h>

class ThreadQueue;

/**
 * A thread that performs queued work.
 */
class ThreadWorker {
	pthread_t thread;

	ThreadQueue &queue;

public:
	/**
	 * Throws on error.
	 */
	explicit ThreadWorker(ThreadQueue &_queue);

	/**
	 * Wait for the thread to exit.  You must call
	 * ThreadQueue::Stop() prior to this function.
	 */
	void Join() noexcept {
		pthread_join(thread, nullptr);
	}

private:
	void Run() noexcept;
	static void *Run(void *ctx) noexcept;
};
