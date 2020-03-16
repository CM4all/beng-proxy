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

#include "Queue.hxx"
#include "Job.hxx"
#include "Notify.hxx"

#include "util/Compiler.h"

#include <mutex>
#include <condition_variable>

#include <assert.h>

class ThreadQueue {
public:
	std::mutex mutex;
	std::condition_variable cond;

	bool alive = true;

	/**
	 * Was the #wakeup_event triggered?  This avoids duplicate events.
	 */
	bool pending = false;

	typedef boost::intrusive::list<ThreadJob,
				       boost::intrusive::constant_time_size<false>> JobList;

	JobList waiting, busy, done;

	Notify notify;

	explicit ThreadQueue(EventLoop &event_loop) noexcept
		:notify(event_loop, BIND_THIS_METHOD(WakeupCallback)) {}

	~ThreadQueue() noexcept {
		assert(!alive);
	}

	bool IsEmpty() const noexcept {
		return waiting.empty() && busy.empty() && done.empty();
	}

	void WakeupCallback() noexcept;
};

void
ThreadQueue::WakeupCallback() noexcept
{
	mutex.lock();

	pending = false;

	for (auto i = done.begin(), end = done.end(); i != end;) {
		ThreadJob *job = &*i;
		assert(job->state == ThreadJob::State::DONE);

		i = done.erase(i);

		if (job->again) {
			/* schedule this job again */
			job->state = ThreadJob::State::WAITING;
			job->again = false;
			waiting.push_back(*job);
			cond.notify_one();
		} else {
			job->state = ThreadJob::State::INITIAL;
			mutex.unlock();
			job->Done();
			mutex.lock();
		}
	}

	const bool empty = IsEmpty();

	mutex.unlock();

	if (empty)
		notify.Disable();
}

ThreadQueue *
thread_queue_new(EventLoop &event_loop) noexcept
{
	return new ThreadQueue(event_loop);
}

void
thread_queue_stop(ThreadQueue &q) noexcept
{
	std::unique_lock<std::mutex> lock(q.mutex);
	q.alive = false;
	q.cond.notify_all();
}

void
thread_queue_free(ThreadQueue *q) noexcept
{
	delete q;
}

void
thread_queue_add(ThreadQueue &q, ThreadJob &job) noexcept
{
	q.mutex.lock();
	assert(q.alive);

	if (job.state == ThreadJob::State::INITIAL) {
		job.state = ThreadJob::State::WAITING;
		job.again = false;
		q.waiting.push_back(job);
		q.cond.notify_one();
	} else if (job.state != ThreadJob::State::WAITING) {
		job.again = true;
	}

	q.mutex.unlock();

	q.notify.Enable();
}

ThreadJob *
thread_queue_wait(ThreadQueue &q) noexcept
{
	std::unique_lock<std::mutex> lock(q.mutex);

	while (true) {
		if (!q.alive)
			return nullptr;

		auto i = q.waiting.begin();
		if (i != q.waiting.end()) {
			auto &job = *i;
			assert(job.state == ThreadJob::State::WAITING);

			job.state = ThreadJob::State::BUSY;
			q.waiting.erase(i);
			q.busy.push_back(job);
			return &job;
		}

		/* queue is empty, wait for a new job to be added */
		q.cond.wait(lock);
	}
}

void
thread_queue_done(ThreadQueue &q, ThreadJob &job) noexcept
{
	assert(job.state == ThreadJob::State::BUSY);

	q.mutex.lock();

	job.state = ThreadJob::State::DONE;
	q.busy.erase(q.busy.iterator_to(job));
	q.done.push_back(job);

	q.pending = true;

	q.mutex.unlock();

	q.notify.Signal();
}

bool
thread_queue_cancel(ThreadQueue &q, ThreadJob &job) noexcept
{
	std::unique_lock<std::mutex> lock(q.mutex);

	switch (job.state) {
	case ThreadJob::State::INITIAL:
		/* already idle */
		return true;

	case ThreadJob::State::WAITING:
		/* cancel it */
		q.waiting.erase(q.waiting.iterator_to(job));
		job.state = ThreadJob::State::INITIAL;
		return true;

	case ThreadJob::State::BUSY:
		/* no chance */
		return false;

	case ThreadJob::State::DONE:
		/* TODO: the callback hasn't been invoked yet - do that now?
		   anyway, with this pending state, we can't return success */
		return false;
	}

	assert(false);
	gcc_unreachable();
}
