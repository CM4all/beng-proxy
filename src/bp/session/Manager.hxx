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

#include "event/TimerEvent.hxx"
#include "util/Compiler.h"

#include <chrono>

struct Session;
struct SessionContainer;
class SessionId;
class SessionLease;
class EventLoop;

class SessionManager {
	/** clean up expired sessions every 60 seconds */
	static constexpr Event::Duration cleanup_interval = std::chrono::minutes(1);

	const unsigned cluster_size, cluster_node;

	SessionContainer *container;

	TimerEvent cleanup_timer;

public:
	SessionManager(EventLoop &event_loop, std::chrono::seconds idle_timeout,
		       unsigned _cluster_size, unsigned _cluster_node) noexcept;

	~SessionManager() noexcept;

	/**
	 * Re-add all libevent events after DisableEvents().
	 */
	void EnableEvents() noexcept {
		cleanup_timer.Schedule(cleanup_interval);
	}

	/**
	 * Removes all libevent events.  Call this before fork(), or
	 * before creating a new event base.  Don't forget to call
	 * EnableEvents() afterwards.
	 */
	void DisableEvents() noexcept {
		cleanup_timer.Cancel();
	}

	void AdjustNewSessionId(SessionId &id) const noexcept;

	/**
	 * Returns the number of sessions.
	 */
	gcc_pure
	unsigned Count() noexcept;

	/**
	 * Invoke the callback for each session.  The session and the
	 * session manager will be locked during the callback.
	 */
	bool Visit(bool (*callback)(const Session *session,
				    void *ctx), void *ctx);

	gcc_pure
	SessionLease Find(SessionId id) noexcept;

	void Put(Session &session) noexcept;

	/**
	 * Add an initialized #Session object to the session manager.  It
	 * will be destroyed automatically when the session
	 * expires.  After returning from this function, the session is
	 * protected and the pointer must not be used, unless it is looked
	 * up (and thus locked).
	 */
	void Insert(Session &session) noexcept;
	void EraseAndDispose(SessionId id) noexcept;

	SessionLease CreateSession() noexcept;

	bool Purge() noexcept;

	void Cleanup() noexcept;

private:
	SessionId GenerateSessionId() const noexcept;
};
