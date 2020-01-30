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

#include "Pool.hxx"
#include "Queue.hxx"
#include "Worker.hxx"
#include "io/Logger.hxx"

#include <array>

#include <assert.h>
#include <stdlib.h>

static ThreadQueue *global_thread_queue;
static std::array<struct thread_worker, 8> worker_threads;

static void
thread_pool_init(EventLoop &event_loop) noexcept
{
	global_thread_queue = thread_queue_new(event_loop);
}

static void
thread_pool_start() noexcept
try {
	assert(global_thread_queue != nullptr);

	for (auto &i : worker_threads)
		thread_worker_create(i, *global_thread_queue);
} catch (...) {
	LogConcat(1, "thread_pool", "Failed to launch worker thread: ",
		  std::current_exception());
	exit(EXIT_FAILURE);
}

ThreadQueue &
thread_pool_get_queue(EventLoop &event_loop) noexcept
{
	if (global_thread_queue == nullptr) {
		/* initial call - create the queue and launch worker
		   threads */
		thread_pool_init(event_loop);
		thread_pool_start();
	}

	return *global_thread_queue;
}

void
thread_pool_stop() noexcept
{
	if (global_thread_queue == nullptr)
		return;

	thread_queue_stop(*global_thread_queue);
}

void
thread_pool_join() noexcept
{
	if (global_thread_queue == nullptr)
		return;

	for (auto &i : worker_threads)
		thread_worker_join(i);
}

void
thread_pool_deinit() noexcept
{
	if (global_thread_queue == nullptr)
		return;

	thread_queue_free(global_thread_queue);
	global_thread_queue = nullptr;
}
