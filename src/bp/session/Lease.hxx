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

/*
 * Session management.
 */

#pragma once

#include "Id.hxx"

struct Session;
struct RealmSession;
class SessionManager;

/**
 * Finds the session with the specified id.  The returned object is
 * locked, and must be unlocked with session_put().
 */
Session *
session_get(SessionId id) noexcept;

class SessionLease {
	friend class RealmSessionLease;

	Session *session = nullptr;
	SessionManager *manager;

public:
	SessionLease() noexcept = default;
	SessionLease(std::nullptr_t) noexcept {}

	SessionLease(SessionManager &_manager, SessionId id) noexcept;

	SessionLease(SessionManager &_manager, Session *_session) noexcept
		:session(_session), manager(&_manager) {}

	SessionLease(SessionLease &&src) noexcept
		:session(std::exchange(src.session, nullptr)),
		 manager(src.manager) {}

	~SessionLease() noexcept {
		if (session != nullptr)
			Put(*manager, *session);
	}

	SessionLease &operator=(SessionLease &&src) noexcept {
		using std::swap;
		swap(manager, src.manager);
		swap(session, src.session);
		return *this;
	}

	operator bool() const noexcept {
		return session != nullptr;
	}

	Session &operator *() const noexcept {
		return *session;
	}

	Session *operator->() const noexcept {
		return session;
	}

	Session *get() const noexcept {
		return session;
	}

private:
	static void Put(SessionManager &manager, Session &session) noexcept;
};

class RealmSessionLease {
	RealmSession *session = nullptr;
	SessionManager *manager;

public:
	RealmSessionLease() noexcept = default;
	RealmSessionLease(std::nullptr_t) noexcept {}

	RealmSessionLease(SessionLease &&src, const char *realm) noexcept;

	RealmSessionLease(SessionManager &_manager,
			  SessionId id, const char *realm) noexcept;

	explicit RealmSessionLease(SessionManager &_manager, RealmSession *_session) noexcept
		:session(_session), manager(&_manager) {}

	RealmSessionLease(RealmSessionLease &&src) noexcept
		:session(std::exchange(src.session, nullptr)),
		 manager(src.manager) {}

	~RealmSessionLease() noexcept {
		if (session != nullptr)
			Put(*manager, *session);
	}

	RealmSessionLease &operator=(RealmSessionLease &&src) noexcept {
		using std::swap;
		swap(manager, src.manager);
		swap(session, src.session);
		return *this;
	}

	operator bool() const noexcept {
		return session != nullptr;
	}

	RealmSession &operator *() const noexcept {
		return *session;
	}

	RealmSession *operator->() const noexcept {
		return session;
	}

	RealmSession *get() const noexcept {
		return session;
	}

private:
	static void Put(SessionManager &manager, RealmSession &session) noexcept;
};
