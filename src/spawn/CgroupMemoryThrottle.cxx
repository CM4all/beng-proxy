// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CgroupMemoryThrottle.hxx"
#include "spawn/ProcessHandle.hxx"
#include "event/Loop.hxx"
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
	 light_pressure_threshold(limit / 10 * 9),
	 heavy_pressure_threshold(limit / 16 * 15),
	 watch(event_loop, group_fd, BIND_THIS_METHOD(OnMemoryWarning)),
	 repeat_timer(event_loop, BIND_THIS_METHOD(OnRepeatTimer)),
	 next_spawn_service(_next_spawn_service),
	 retry_waiting_timer(event_loop, BIND_THIS_METHOD(OnRetryWaitingTimer)) {}

CgroupMemoryThrottle::~CgroupMemoryThrottle() noexcept = default;

inline uint_least64_t
CgroupMemoryThrottle::GetMemoryUsage() const noexcept
{
	try {
		return watch.GetMemoryUsage();
	} catch (...) {
		PrintException(std::current_exception());
		return 0;
	}
}

inline uint_least64_t
CgroupMemoryThrottle::IsUnderPressure(uint_least64_t threshold) const noexcept
{
	const uint_least64_t usage = GetMemoryUsage();
	return usage >= threshold ? usage : 0;
}

inline void
CgroupMemoryThrottle::OnMemoryWarning(uint_least64_t usage) noexcept
{
	last_check = GetEventLoop().SteadyNow();

	if (limit > 0 && usage < light_pressure_threshold)
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

	last_check = GetEventLoop().SteadyNow();

	const uint_least64_t usage = IsUnderLightPressure();
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

inline void
CgroupMemoryThrottle::MaybeCheckMemoryWarning() noexcept
{
	if (limit == 0)
		// no limit configured
		return;

	const auto now = GetEventLoop().SteadyNow();
	if (now < last_check + std::chrono::seconds{1})
		// we already checked recently
		return;

	last_check = now;

	const auto usage = IsUnderLightPressure();
	if (usage == 0)
		return;

	fmt::print(stderr, "Spawner memory warning: {} of {} bytes used\n",
		   usage, limit);

	callback();

	repeat_timer.Schedule(std::chrono::seconds{2});
}

std::unique_ptr<ChildProcessHandle>
CgroupMemoryThrottle::SpawnChildProcess(std::string_view name, PreparedChildProcess &&params)
{
	return next_spawn_service.SpawnChildProcess(name, std::move(params));
}

void
CgroupMemoryThrottle::Enqueue(EnqueueCallback _callback, CancellablePointer &cancel_ptr) noexcept
{
	if (!repeat_timer.IsPending() && !retry_waiting_timer.IsPending()) {
		/* check for memory warnings to prevent running into
                   the kernel shrinker */
		MaybeCheckMemoryWarning();

		next_spawn_service.Enqueue(_callback, cancel_ptr);
		return;
	}

	assert(limit > 0);

	if (!IsUnderHeavyPressure()) {
		next_spawn_service.Enqueue(_callback, cancel_ptr);
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

	if (IsUnderHeavyPressure()) {
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
