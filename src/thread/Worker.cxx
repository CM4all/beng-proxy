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

#include "Worker.hxx"
#include "Queue.hxx"
#include "Job.hxx"
#include "ssl/Init.hxx"
#include "system/Error.hxx"
#include "util/ScopeExit.hxx"

static void *
thread_worker_run(void *ctx) noexcept
{
	/* reduce glibc's thread cancellation overhead */
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);

	struct thread_worker &w = *(struct thread_worker *)ctx;
	ThreadQueue &q = *w.queue;

	ThreadJob *job;
	while ((job = thread_queue_wait(q)) != nullptr) {
		job->Run();
		thread_queue_done(q, *job);
	}

	ssl_thread_deinit();

	return nullptr;
}

void
thread_worker_create(struct thread_worker &w, ThreadQueue &q)
{
	w.queue = &q;

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	AtScopeExit(&attr) { pthread_attr_destroy(&attr); };

	/* 64 kB stack ought to be enough */
	pthread_attr_setstacksize(&attr, 65536);

	int error = pthread_create(&w.thread, &attr, thread_worker_run, &w);
	if (error != 0)
		throw MakeErrno(error, "Failed to create worker thread");
}
