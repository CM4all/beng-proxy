// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Session management.
 */

#pragma once

#include "Id.hxx"

#include <utility>

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

	RealmSessionLease(SessionLease &&src, std::string_view realm) noexcept;

	RealmSessionLease(SessionManager &_manager,
			  SessionId id, std::string_view realm) noexcept;

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
