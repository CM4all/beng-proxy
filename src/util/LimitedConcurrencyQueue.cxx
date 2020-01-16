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
