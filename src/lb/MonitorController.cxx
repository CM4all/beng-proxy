// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "MonitorController.hxx"
#include "MonitorConfig.hxx"
#include "MonitorClass.hxx"
#include "event/Loop.hxx"
#include "net/FailureManager.hxx"

#include <fmt/core.h>

void
LbMonitorController::Success()
{
	cancel_ptr = nullptr;
	timeout_event.Cancel();

	if (!state)
		logger(5, "recovered");
	else if (fade)
		logger(5, "finished fade");
	else
		logger(6, "ok");

	state = true;

	failure->UnsetMonitor();

	if (fade) {
		fade = false;
		failure->UnsetFade();
	}

	interval_event.Schedule(config.interval);
}

void
LbMonitorController::Fade()
{
	cancel_ptr = nullptr;
	timeout_event.Cancel();

	if (!fade)
		logger(5, "fade");
	else
		logger(6, "still fade");

	fade = true;
	failure->SetFade(event_loop.SteadyNow(), std::chrono::minutes(5));

	interval_event.Schedule(config.interval);
}

void
LbMonitorController::Timeout()
{
	cancel_ptr = nullptr;
	timeout_event.Cancel();

	logger(state ? 3 : 6, "timeout");

	state = false;
	failure->SetMonitor();

	interval_event.Schedule(config.interval);
}

void
LbMonitorController::Error(std::exception_ptr e)
{
	cancel_ptr = nullptr;
	timeout_event.Cancel();

	logger(state ? 2 : 4, "error: ", e);

	state = false;
	failure->SetMonitor();

	interval_event.Schedule(config.interval);
}

inline void
LbMonitorController::IntervalCallback() noexcept
{
	assert(!cancel_ptr);

	logger(6, "run");

	if (config.timeout > Event::Duration{})
		timeout_event.Schedule(config.timeout);

	class_.run(event_loop, config, address, *this, cancel_ptr);
}

inline void
LbMonitorController::TimeoutCallback() noexcept
{
	assert(cancel_ptr);

	logger(6, "timeout");

	cancel_ptr.Cancel();

	state = false;
	failure->SetMonitor();

	interval_event.Schedule(config.interval);
}

static std::string
MakeLoggerDomain(const char *monitor_name, const char *node_name,
		 unsigned port) noexcept
{
	return fmt::format("monitor {}:[{}]:{}",
			   monitor_name, node_name, port);
}

LbMonitorController::LbMonitorController(EventLoop &_event_loop,
					 FailureManager &failure_manager,
					 const char *node_name,
					 const LbMonitorConfig &_config,
					 SocketAddress _address,
					 const LbMonitorClass &_class) noexcept
	:event_loop(_event_loop),
	 failure(failure_manager.Make(_address)),
	 config(_config),
	 address(_address),
	 class_(_class),
	 logger(MakeLoggerDomain(config.name.c_str(), node_name,
				 address.GetPort())),
	 interval_event(event_loop, BIND_THIS_METHOD(IntervalCallback)),
	 timeout_event(event_loop, BIND_THIS_METHOD(TimeoutCallback))
{
	interval_event.Schedule(std::chrono::seconds(0));
}

LbMonitorController::~LbMonitorController() noexcept
{
	if (cancel_ptr)
		cancel_ptr.Cancel();
}
