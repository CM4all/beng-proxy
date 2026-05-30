// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "stats/CgroupPressureStats.hxx"
#include "spawn/CgroupMemoryWatch.hxx"
#include "spawn/Interface.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "event/FineTimerEvent.hxx"
#include "util/IntrusiveList.hxx"

class CgroupPressureHandler;

/**
 * Wraps #CgroupMemoryWatch and adds a timer that checks whether we
 * have fallen below the configured limit.
 *
 * Additionally, implements the #SpawnService interface which
 * throttles the Enqueue() method as long as we're under pressure.
 */
class CgroupMemoryThrottle final : public SpawnService {
	static constexpr uint_least64_t KILO = 1024;
	static constexpr uint_least64_t MEGA = 1024 * KILO;
	static constexpr uint_least64_t GIGA = 1024 * MEGA;

	/**
	 * The pressure thresholds are calculated as a percentage of
	 * the configured memory limit (memory.high or memory.max),
	 * but the clearance from this limit may not be larger than
	 * these numbers.  This avoids excessive reservations on large
	 * servers with lots of RAM.
	 */
	static constexpr uint_least64_t MAX_LIGHT_PRESSURE_CLEARANCE = 8 * GIGA;
	static constexpr uint_least64_t MAX_HEAVY_PRESSURE_CLEARANCE = 2 * GIGA;

	CgroupPressureHandler &handler;

	/**
	 * The configured memory limit [bytes].  Zero if none is
	 * configured.
	 */
	const uint_least64_t limit;

	/**
	 * Memory usage above this value means "under light pressure".
	 * In this state, unused processes are stopped.
	 */
	const uint_least64_t light_pressure_threshold;

	/**
	 * Memory usage above this value means "under heavy pressure".
	 * In this state, no new processes will be spawned.
	 */
	const uint_least64_t heavy_pressure_threshold;

	CgroupMemoryWatch watch;

	/**
	 * This timer repeats the memory pressure check periodically
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
	 * When did we last check for a memory warning?  This
	 * throttles reading from the "memory.current" file.
	 */
	Event::TimePoint last_check;

	unsigned repeat_counter = 0;

	CgroupPressureStats stats;

public:
	CgroupMemoryThrottle(EventLoop &event_loop,
			     FileDescriptor group_fd,
			     SpawnService &_next_spawn_service,
			     CgroupPressureHandler &_handler,
			     uint_least64_t _limit);

	~CgroupMemoryThrottle() noexcept;

	auto &GetEventLoop() const noexcept {
		return watch.GetEventLoop();
	}

	const auto &GetStats() const noexcept {
		return stats;
	}

private:
	static constexpr uint_least64_t CalculateLightPressureThreshold(uint_least64_t limit) noexcept {
		uint_least64_t threshold = limit / 10 * 9;
		if (threshold + MAX_LIGHT_PRESSURE_CLEARANCE < limit)
			threshold = limit - MAX_LIGHT_PRESSURE_CLEARANCE;
		return threshold;
	}

	static constexpr uint_least64_t CalculateHeavyPressureThreshold(uint_least64_t limit) noexcept {
		uint_least64_t threshold = limit / 16 * 15;
		if (threshold + MAX_HEAVY_PRESSURE_CLEARANCE < limit)
			threshold = limit - MAX_HEAVY_PRESSURE_CLEARANCE;
		return threshold;
	}

	/**
	 * A non-throwing wrapper for
	 * CgroupMemoryWatch::GetMemoryUsage().  Errors are logged to
	 * #stderr (returning 0).
	 */
	[[gnu::pure]]
	uint_least64_t GetMemoryUsage() const noexcept;

	/**
	 * Returns 0 if we're below #pressure_threshold, or the
	 * current memory usage if we're above the threshold
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

	void OnMemoryWarning(uint_least64_t memory_usage) noexcept;
	void OnRepeatTimer() noexcept;
	void MaybeCheckMemoryWarning() noexcept;
	void OnRetryWaitingTimer() noexcept;

	// virtual methods from SpawnService
	std::unique_ptr<ChildProcessHandle> SpawnChildProcess(std::string_view name,
							      PreparedChildProcess &&params) override;
	void Enqueue(EnqueueCallback callback, CancellablePointer &cancel_ptr) noexcept override;
};
