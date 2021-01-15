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

/**
 * Finds the session with the specified id.  The returned object is
 * locked, and must be unlocked with session_put().
 */
Session *
session_get(SessionId id) noexcept;

/**
 * Unlocks the specified session.
 */
void
session_put(Session *session) noexcept;

void
session_put(RealmSession &session) noexcept;

class SessionLease {
	friend class RealmSessionLease;

	Session *session = nullptr;

public:
	SessionLease() noexcept = default;
	SessionLease(std::nullptr_t) noexcept {}

	explicit SessionLease(SessionId id) noexcept
		:session(session_get(id)) {}

	explicit SessionLease(Session *_session) noexcept
		:session(_session) {}

	SessionLease(SessionLease &&src) noexcept
		:session(std::exchange(src.session, nullptr)) {}

	~SessionLease() noexcept {
		if (session != nullptr)
			session_put(session);
	}

	SessionLease &operator=(SessionLease &&src) noexcept {
		using std::swap;
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
};

class RealmSessionLease {
	RealmSession *session = nullptr;

public:
	RealmSessionLease() noexcept = default;
	RealmSessionLease(std::nullptr_t) noexcept {}

	RealmSessionLease(SessionLease &&src, const char *realm) noexcept;

	RealmSessionLease(SessionId id, const char *realm) noexcept;

	explicit RealmSessionLease(RealmSession *_session) noexcept
		:session(_session) {}

	RealmSessionLease(RealmSessionLease &&src) noexcept
		:session(std::exchange(src.session, nullptr)) {}

	~RealmSessionLease() noexcept {
		if (session != nullptr)
			session_put(*session);
	}

	RealmSessionLease &operator=(RealmSessionLease &&src) noexcept {
		using std::swap;
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
};
