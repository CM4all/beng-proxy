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

#pragma once

#include "MonitorHandler.hxx"
#include "io/Logger.hxx"
#include "event/TimerEvent.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/FailureRef.hxx"
#include "util/Cancellable.hxx"

class EventLoop;
class FailureManager;
class SocketAddress;
struct LbMonitorConfig;
struct LbMonitorClass;
class LbMonitorController;

class LbMonitorController final : public LbMonitorHandler {
	EventLoop &event_loop;
	FailureRef failure;

	const LbMonitorConfig &config;
	const AllocatedSocketAddress address;
	const LbMonitorClass &class_;

	const Logger logger;

	TimerEvent interval_event;
	TimerEvent timeout_event;

	CancellablePointer cancel_ptr;

	bool state = true;
	bool fade = false;

	unsigned ref = 0;

public:
	LbMonitorController(EventLoop &_event_loop,
			    FailureManager &_failure_manager,
			    const char *node_name,
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
