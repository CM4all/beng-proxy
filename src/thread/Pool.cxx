// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * A queue that manages work for worker threads.
 */

#include "Pool.hxx"
#include "Queue.hxx"
#include "Worker.hxx"
#include "io/Logger.hxx"

#include <forward_list>

#include <assert.h>
#include <stdlib.h>
#include <sys/sysinfo.h>

static ThreadQueue *global_thread_queue;
static bool global_thread_queue_volatile = false;
static std::forward_list<ThreadWorker> worker_threads;

static void
thread_pool_init(EventLoop &event_loop) noexcept
{
	global_thread_queue = new ThreadQueue(event_loop);
}

[[gnu::const]]
static unsigned
GetWorkerThreadCount() noexcept
{
	const int nprocs = get_nprocs();
	if (nprocs <= 1)
		return 1;

	unsigned n = static_cast<unsigned>(nprocs);

	/* no more than 16 threads */
	static constexpr unsigned MAX_WORKER_THREADS = 16;
	if (n > MAX_WORKER_THREADS)
		n = MAX_WORKER_THREADS;

	return n;
}

static void
thread_pool_start() noexcept
try {
	assert(global_thread_queue != nullptr);

	const unsigned n_worker_threads = GetWorkerThreadCount();
	for (unsigned i = 0; i < n_worker_threads; ++i) {
		worker_threads.emplace_front(*global_thread_queue);
	}
} catch (...) {
	LogConcat(1, "thread_pool", "Failed to launch worker thread: ",
		  std::current_exception());
	if (worker_threads.empty())
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

		if (global_thread_queue_volatile)
			global_thread_queue->SetVolatile();
	}

	return *global_thread_queue;
}

void
thread_pool_set_volatile() noexcept
{
	global_thread_queue_volatile = true;

	if (global_thread_queue != nullptr)
		global_thread_queue->SetVolatile();
}

void
thread_pool_stop() noexcept
{
	if (global_thread_queue == nullptr)
		return;

	global_thread_queue->Stop();
}

void
thread_pool_join() noexcept
{
	if (global_thread_queue == nullptr)
		return;

	while (!worker_threads.empty()) {
		worker_threads.front().Join();
		worker_threads.pop_front();
	}
}

void
thread_pool_deinit() noexcept
{
	if (global_thread_queue == nullptr)
		return;

	delete global_thread_queue;
	global_thread_queue = nullptr;
}
