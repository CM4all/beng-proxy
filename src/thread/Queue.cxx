/*
 * Copyright 2007-2022 CM4all GmbH
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
#include "util/Compiler.h"

#include <assert.h>

ThreadQueue::ThreadQueue(EventLoop &event_loop) noexcept
	:notify(event_loop, BIND_THIS_METHOD(WakeupCallback))
{
}

ThreadQueue::~ThreadQueue() noexcept
{
	assert(!alive);
}

void
ThreadQueue::WakeupCallback() noexcept
{
	mutex.lock();

	done.clear_and_dispose([this](auto *_job){
		auto &job = *_job;
		assert(job.state == ThreadJob::State::DONE);

		if (job.again) {
			/* schedule this job again */
			job.state = ThreadJob::State::WAITING;
			job.again = false;
			waiting.push_back(job);
			cond.notify_one();
		} else {
			job.state = ThreadJob::State::INITIAL;
			mutex.unlock();
			job.Done();
			mutex.lock();
		}
	});

	CheckDisableNotify();

	mutex.unlock();
}

void
ThreadQueue::Stop() noexcept
{
	const std::scoped_lock lock{mutex};
	alive = false;
	cond.notify_all();

	volatile_notify = true;
	CheckDisableNotify();
}

void
ThreadQueue::Add(ThreadJob &job) noexcept
{
	mutex.lock();
	assert(alive);

	if (job.state == ThreadJob::State::INITIAL) {
		job.state = ThreadJob::State::WAITING;
		job.again = false;
		waiting.push_back(job);
		cond.notify_one();
	} else if (job.state != ThreadJob::State::WAITING) {
		job.again = true;
	}

	mutex.unlock();

	notify.Enable();
}

ThreadJob *
ThreadQueue::Wait() noexcept
{
	std::unique_lock lock{mutex};

	while (true) {
		if (!alive)
			return nullptr;

		auto i = waiting.begin();
		if (i != waiting.end()) {
			auto &job = *i;
			assert(job.state == ThreadJob::State::WAITING);

			job.state = ThreadJob::State::BUSY;
			job.unlink();
			busy.push_back(job);
			return &job;
		}

		/* queue is empty, wait for a new job to be added */
		cond.wait(lock);
	}
}

void
ThreadQueue::Done(ThreadJob &job) noexcept
{
	assert(job.state == ThreadJob::State::BUSY);

	mutex.lock();

	job.state = ThreadJob::State::DONE;
	job.unlink();
	done.push_back(job);

	mutex.unlock();

	notify.Signal();
}

bool
ThreadQueue::Cancel(ThreadJob &job) noexcept
{
	const std::scoped_lock lock{mutex};

	switch (job.state) {
	case ThreadJob::State::INITIAL:
		/* already idle */
		return true;

	case ThreadJob::State::WAITING:
		/* cancel it */
		job.unlink();
		job.state = ThreadJob::State::INITIAL;
		CheckDisableNotify();
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
