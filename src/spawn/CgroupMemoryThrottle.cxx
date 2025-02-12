// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CgroupMemoryThrottle.hxx"
#include "spawn/ProcessHandle.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <fmt/core.h>

#include <cassert>

struct CgroupMemoryThrottle::Waiting final : IntrusiveListHook<IntrusiveHookMode::AUTO_UNLINK>, Cancellable {
	const EnqueueCallback callback;
	CancellablePointer &cancel_ptr;

	Waiting(EnqueueCallback _callback,
		CancellablePointer &_cancel_ptr) noexcept
		:callback(_callback), cancel_ptr(_cancel_ptr) {
		cancel_ptr = *this;
	}

	// virtual methods from Cancellable
	void Cancel() noexcept override {
		delete this;
	}
};

CgroupMemoryThrottle::CgroupMemoryThrottle(EventLoop &event_loop,
					   FileDescriptor group_fd,
					   SpawnService &_next_spawn_service,
					   BoundMethod<void() noexcept> _callback,
					   uint_least64_t _limit)
	:callback(_callback),
	 limit(_limit),
	 pressure_threshold(limit / 16 * 15),
	 watch(event_loop, group_fd, BIND_THIS_METHOD(OnMemoryWarning)),
	 repeat_timer(event_loop, BIND_THIS_METHOD(OnRepeatTimer)),
	 next_spawn_service(_next_spawn_service),
	 retry_waiting_timer(event_loop, BIND_THIS_METHOD(OnRetryWaitingTimer)) {}

CgroupMemoryThrottle::~CgroupMemoryThrottle() noexcept = default;

uint_least64_t
CgroupMemoryThrottle::IsUnderPressure() const noexcept
{
	assert(limit > 0);

	try {
		const auto usage = watch.GetMemoryUsage();
		return usage >= pressure_threshold ? usage : 0;
	} catch (...) {
		PrintException(std::current_exception());
		return 0;
	}
}

inline void
CgroupMemoryThrottle::OnMemoryWarning(uint_least64_t usage) noexcept
{
	if (limit > 0 && usage < pressure_threshold)
		/* false alarm - we're well below the configured
		   limit */
		return;

	fmt::print(stderr, "Spawner memory warning: {} of {} bytes used\n",
		   usage, limit);

	callback();

	if (limit > 0)
		repeat_timer.ScheduleEarlier(std::chrono::seconds{2});
}

inline void
CgroupMemoryThrottle::OnRepeatTimer() noexcept
{
	assert(limit > 0);

	const uint_least64_t usage = IsUnderPressure();
	if (usage == 0)
		return;

	/* repeat until we have a safe margin below the configured
	   memory limit to avoid too much kernel shrinker
	   contention */

	fmt::print(stderr, "Spawner memory warning (repeat): {} of {} bytes used\n",
		   usage, limit);

	callback();

	repeat_timer.Schedule(std::chrono::seconds{2});
}

std::unique_ptr<ChildProcessHandle>
CgroupMemoryThrottle::SpawnChildProcess(const char *name, PreparedChildProcess &&params)
{
	return next_spawn_service.SpawnChildProcess(name, std::move(params));
}

void
CgroupMemoryThrottle::Enqueue(EnqueueCallback _callback, CancellablePointer &cancel_ptr) noexcept
{
	if (!repeat_timer.IsPending() && !retry_waiting_timer.IsPending()) {
		_callback();
		return;
	}

	assert(limit > 0);

	if (!IsUnderPressure()) {
		_callback();
		return;
	}

	auto *w = new Waiting(_callback, cancel_ptr);
	waiting.push_back(*w);

	retry_waiting_timer.ScheduleEarlier(std::chrono::milliseconds{250});
}

inline void
CgroupMemoryThrottle::OnRetryWaitingTimer() noexcept
{
	if (waiting.empty())
		// all waiters were canceled
		return;

	if (IsUnderPressure()) {
		// still under pressure - try again later
		retry_waiting_timer.Schedule(std::chrono::milliseconds{100});
		return;
	}

	/* below the threshold - handle one Enqueue() callback */
	waiting.pop_front_and_dispose([this](auto *w){
		next_spawn_service.Enqueue(w->callback, w->cancel_ptr);
		delete w;
	});

	/* re-schedule the timer to handle more Enqueue() callbacks
	   really soon; this is throttled using the timer to avoid
	   running into memory pressure again */
	if (!waiting.empty())
		retry_waiting_timer.Schedule(std::chrono::milliseconds{20});
}
