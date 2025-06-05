// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "MonitorHandler.hxx"
#include "io/Logger.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/FailureRef.hxx"
#include "util/Cancellable.hxx"

class EventLoop;
class FailureManager;
class SocketAddress;
struct LbMonitorConfig;
struct LbMonitorClass;

class LbMonitorController final : public LbMonitorHandler {
	EventLoop &event_loop;
	FailureRef failure;

	const LbMonitorConfig &config;
	const AllocatedSocketAddress address;
	const LbMonitorClass &class_;

	const Logger logger;

	CoarseTimerEvent interval_event;
	CoarseTimerEvent timeout_event;

	CancellablePointer cancel_ptr;

	bool state = true;
	bool fade = false;

	unsigned ref = 0;

public:
	LbMonitorController(EventLoop &_event_loop,
			    FailureManager &_failure_manager,
			    std::string_view node_name,
			    const LbMonitorConfig &_config,
			    SocketAddress _address,
			    const LbMonitorClass &_class) noexcept;

	~LbMonitorController() noexcept;

	LbMonitorController(const LbMonitorController &) = delete;
	LbMonitorController &operator=(const LbMonitorController &) = delete;

	void Ref() noexcept {
		++ref;
	}

	/**
	 * @return true if the reference counter has dropped to 0 (and the
	 * object can be deleted)
	 */
	bool Unref() noexcept {
		return --ref == 0;
	}

	const SocketAddress GetAddress() const noexcept {
		return address;
	}

private:
	void IntervalCallback() noexcept;
	void TimeoutCallback() noexcept;

	/* virtual methods from class LbMonitorHandler */
	virtual void Success() override;
	virtual void Fade() override;
	virtual void Timeout() override;
	virtual void Error(std::exception_ptr e) override;
};
