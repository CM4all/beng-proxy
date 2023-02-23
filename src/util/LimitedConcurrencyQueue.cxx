// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "LimitedConcurrencyQueue.hxx"

#include <assert.h>

void
LimitedConcurrencyJob::Schedule() noexcept
{
	assert(state == State::NONE);

	queue.Add(*this);
}

void
LimitedConcurrencyJob::Cancel() noexcept
{
	queue.Remove(*this);

	assert(state == State::NONE);
}

LimitedConcurrencyQueue::~LimitedConcurrencyQueue() noexcept
{
	assert(waiting.empty());
	assert(running.empty());
}

void
LimitedConcurrencyQueue::Add(LimitedConcurrencyJob &job) noexcept
{
	if (waiting.empty() && running.size() < concurrency_limit) {
		job.state = LimitedConcurrencyJob::State::RUNNING;
		running.push_back(job);
		job.callback();
	} else {
		job.state = LimitedConcurrencyJob::State::WAITING;
		waiting.push_back(job);
	}
}

void
LimitedConcurrencyQueue::Remove(LimitedConcurrencyJob &job) noexcept
{
	switch (job.state) {
	case LimitedConcurrencyJob::State::NONE:
		break;

	case LimitedConcurrencyJob::State::WAITING:
		waiting.erase(waiting.iterator_to(job));
		job.state = LimitedConcurrencyJob::State::NONE;
		break;

	case LimitedConcurrencyJob::State::RUNNING:
		if (running.size() == concurrency_limit)
			defer_start.Schedule();

		running.erase(running.iterator_to(job));
		job.state = LimitedConcurrencyJob::State::NONE;
		break;
	}
}

void
LimitedConcurrencyQueue::OnDeferredStart() noexcept
{
	if (waiting.empty() || running.size() >= concurrency_limit)
		return;

	auto &job = waiting.front();
	assert(job.state == LimitedConcurrencyJob::State::WAITING);
	job.state = LimitedConcurrencyJob::State::RUNNING;

	waiting.pop_front();
	running.push_back(job);

	if (!waiting.empty() && running.size() < concurrency_limit)
		/* we have more room - schedule another job */
		defer_start.Schedule();

	job.callback();
}
