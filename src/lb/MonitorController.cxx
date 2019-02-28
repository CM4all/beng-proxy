/*
 * Copyright 2007-2017 Content Management AG
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

#include "MonitorController.hxx"
#include "MonitorConfig.hxx"
#include "MonitorClass.hxx"
#include "event/Loop.hxx"
#include "net/FailureManager.hxx"
#include "util/StringFormat.hxx"

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

    failure->UnsetMonitor(event_loop.SteadyNow());

    if (fade) {
        fade = false;
        failure->UnsetFade(event_loop.SteadyNow());
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
    failure->SetMonitor(event_loop.SteadyNow());

    interval_event.Schedule(config.interval);
}

void
LbMonitorController::Error(std::exception_ptr e)
{
    cancel_ptr = nullptr;
    timeout_event.Cancel();

    logger(state ? 2 : 4, "error: ", e);

    state = false;
    failure->SetMonitor(event_loop.SteadyNow());

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

    cancel_ptr.CancelAndClear();

    state = false;
    failure->SetMonitor(event_loop.SteadyNow());

    interval_event.Schedule(config.interval);
}

static std::string
MakeLoggerDomain(const char *monitor_name, const char *node_name,
                 unsigned port) noexcept
{
    return StringFormat<1024>("monitor %s:[%s]:%u",
                              monitor_name, node_name, port).c_str();
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
    static constexpr struct timeval immediately = { 0, 0 };
    interval_event.Add(immediately);
}

LbMonitorController::~LbMonitorController() noexcept
{
    if (cancel_ptr)
        cancel_ptr.Cancel();
}
