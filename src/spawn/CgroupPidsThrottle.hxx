// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "spawn/CgroupPidsWatch.hxx"
#include "spawn/Interface.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "event/FineTimerEvent.hxx"
#include "util/IntrusiveList.hxx"

/**
 * Wraps #CgroupPidsWatch and adds a timer that checks whether we
 * have fallen below the configured limit.
 *
 * Additionally, implements the #SpawnService interface which
 * throttles the Enqueue() method as long as we're under pressure.
 */
class CgroupPidsThrottle final : public SpawnService {
	const BoundMethod<void() noexcept> callback;

	/**
	 * The configured pids limit.  Zero if none is
	 * configured.
	 */
	const uint_least64_t limit;

	/**
	 * Pids usage above this value means "under light pressure".
	 * In this state, unused processes are stopped.
	 */
	const uint_least64_t light_pressure_threshold;

	/**
	 * Pids usage above this value means "under heavy pressure".
	 * In this state, no new processes will be spawned.
	 */
	const uint_least64_t heavy_pressure_threshold;

	CgroupPidsWatch watch;

	/**
	 * This timer repeats the pids pressure check periodically
	 * after pressure was once reported until we're below the
	 * threshold.
	 */
	CoarseTimerEvent repeat_timer;

	SpawnService &next_spawn_service;

	/**
	 * An Enqueue() callback that is waiting for us to go below
	 * the pressure threshold.
	 */
	struct Waiting;
	IntrusiveList<Waiting> waiting;

	/**
	 * Periodically checks if we're below the pressure threshold
	 * and invokes one #waiting item.
	 */
	FineTimerEvent retry_waiting_timer;

	/**
	 * When did we last check for a pids warning?  This
	 * throttles reading from the "pids.current" file.
	 */
	Event::TimePoint last_check;

public:
	CgroupPidsThrottle(EventLoop &event_loop,
			   FileDescriptor group_fd,
			   SpawnService &_next_spawn_service,
			   BoundMethod<void() noexcept> _callback,
			   uint_least64_t _limit);

	~CgroupPidsThrottle() noexcept;

	auto &GetEventLoop() const noexcept {
		return watch.GetEventLoop();
	}

private:
	/**
	 * A non-throwing wrapper for
	 * CgroupPidsWatch::GetPidsCurrent().  Errors are logged to
	 * #stderr (returning 0).
	 */
	[[gnu::pure]]
	uint_least64_t GetPidsCurrent() const noexcept;

	/**
	 * Returns 0 if we're below #threshold, or the current pids
	 * usage if we're above the threshold
	 */
	[[gnu::pure]]
	uint_least64_t IsUnderPressure(uint_least64_t threshold) const noexcept;

	[[gnu::pure]]
	uint_least64_t IsUnderLightPressure() const noexcept {
		return IsUnderPressure(light_pressure_threshold);
	}

	[[gnu::pure]]
	uint_least64_t IsUnderHeavyPressure() const noexcept {
		return IsUnderPressure(heavy_pressure_threshold);
	}

	void OnPidsWarning(uint_least64_t pids_usage) noexcept;
	void OnRepeatTimer() noexcept;
	void MaybeCheckPidsWarning() noexcept;
	void OnRetryWaitingTimer() noexcept;

	// virtual methods from SpawnService
	std::unique_ptr<ChildProcessHandle> SpawnChildProcess(std::string_view name,
							      PreparedChildProcess &&params) override;
	void Enqueue(EnqueueCallback callback, CancellablePointer &cancel_ptr) noexcept override;
};
