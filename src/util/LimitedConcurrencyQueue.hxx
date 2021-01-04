/*
 * Copyright 2007-2021 CM4all GmbH
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

#pragma once

#include "event/DeferEvent.hxx"
#include "util/BindMethod.hxx"

#include <boost/intrusive/list.hpp>

class LimitedConcurrencyQueue;

class LimitedConcurrencyJob final : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
	friend class LimitedConcurrencyQueue;

	LimitedConcurrencyQueue &queue;

	const BoundMethod<void() noexcept> callback;

	enum class State {
		NONE,
		WAITING,
		RUNNING,
	} state = State::NONE;

public:
	LimitedConcurrencyJob(LimitedConcurrencyQueue &_queue,
			      BoundMethod<void() noexcept> _callback) noexcept
		:queue(_queue), callback(_callback) {}

	~LimitedConcurrencyJob() noexcept {
		Cancel();
	}

	LimitedConcurrencyJob(const LimitedConcurrencyJob &) = delete;
	LimitedConcurrencyJob &operator=(const LimitedConcurrencyJob &) = delete;

	bool IsWaiting() const noexcept {
		return state == State::WAITING;
	}

	bool IsRunning() const noexcept {
		return state == State::RUNNING;
	}

	void Schedule() noexcept;
	void Cancel() noexcept;
};

class LimitedConcurrencyQueue {
	using JobList =
		boost::intrusive::list<LimitedConcurrencyJob,
				       boost::intrusive::constant_time_size<true>>;

	JobList waiting, running;

	DeferEvent defer_start;

	const std::size_t concurrency_limit;

public:
	LimitedConcurrencyQueue(EventLoop &event_loop,
				std::size_t _limit) noexcept
		:defer_start(event_loop, BIND_THIS_METHOD(OnDeferredStart)),
		 concurrency_limit(_limit) {}

	~LimitedConcurrencyQueue() noexcept;

	void Add(LimitedConcurrencyJob &job) noexcept;
	void Remove(LimitedConcurrencyJob &job) noexcept;

private:
	void OnDeferredStart() noexcept;
};
