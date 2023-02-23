// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/DeferEvent.hxx"
#include "util/BindMethod.hxx"
#include "util/IntrusiveList.hxx"

class LimitedConcurrencyQueue;

class LimitedConcurrencyJob final
	: public IntrusiveListHook<IntrusiveHookMode::NORMAL>
{
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
		IntrusiveList<LimitedConcurrencyJob,
			      IntrusiveListBaseHookTraits<LimitedConcurrencyJob>,
			      true>;

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
