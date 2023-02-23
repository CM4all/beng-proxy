// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Worker.hxx"
#include "Queue.hxx"
#include "Job.hxx"
#include "system/Error.hxx"
#include "util/ScopeExit.hxx"

inline void
ThreadWorker::Run() noexcept
{
	ThreadJob *job;
	while ((job = queue.Wait()) != nullptr) {
		job->Run();
		queue.Done(*job);
	}
}

void *
ThreadWorker::Run(void *ctx) noexcept
{
	/* reduce glibc's thread cancellation overhead */
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);

	auto &w = *(ThreadWorker *)ctx;
	w.Run();

	return nullptr;
}

ThreadWorker::ThreadWorker(ThreadQueue &_queue)
	:queue(_queue)
{
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	AtScopeExit(&attr) { pthread_attr_destroy(&attr); };

	/* 64 kB stack ought to be enough */
	pthread_attr_setstacksize(&attr, 65536);

	int error = pthread_create(&thread, &attr, Run, this);
	if (error != 0)
		throw MakeErrno(error, "Failed to create worker thread");
}
