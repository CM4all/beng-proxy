// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Queue.hxx"
#include "Job.hxx"
#include "util/Compiler.h"

#include <cassert>

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
	std::unique_lock lock{mutex};

	done.clear_and_dispose([this, &lock](auto *_job){
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
			lock.unlock();
			job.Done();
			lock.lock();
		}
	});

	CheckDisableNotify();
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

inline void
ThreadQueue::_Add(ThreadJob &job) noexcept
{
	assert(alive);

	if (job.state == ThreadJob::State::INITIAL) {
		job.state = ThreadJob::State::WAITING;
		job.again = false;
		waiting.push_back(job);
		cond.notify_one();
	} else if (job.state != ThreadJob::State::WAITING) {
		job.again = true;
	}
}

void
ThreadQueue::Add(ThreadJob &job) noexcept
{
	{
		const std::scoped_lock lock{mutex};
		_Add(job);
	}

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

	{
		const std::scoped_lock lock{mutex};

		job.state = ThreadJob::State::DONE;
		job.unlink();
		done.push_back(job);
	}

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
