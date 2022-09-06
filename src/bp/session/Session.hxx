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

/*
 * Session management.
 */

#pragma once

#include "Id.hxx"
#include "http/CookieJar.hxx"
#include "shm/String.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Expiry.hxx"

#include "util/Compiler.h"

#include <boost/intrusive/set.hpp>
#include <boost/intrusive/unordered_set_hook.hpp>

#include <chrono>

#include <string.h>

struct dpool;
struct RealmSession;
struct HttpAddress;

/**
 * Session data associated with a widget instance (struct widget).
 */
struct WidgetSession
	: boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

	struct Compare {
		bool operator()(const WidgetSession &a, const WidgetSession &b) const {
			return strcmp(a.id, b.id) < 0;
		}

		bool operator()(const WidgetSession &a, const char *b) const {
			return strcmp(a.id, b) < 0;
		}

		bool operator()(const char *a, const WidgetSession &b) const {
			return strcmp(a, b.id) < 0;
		}
	};

	using Set = boost::intrusive::set<WidgetSession,
					  boost::intrusive::compare<Compare>,
					  boost::intrusive::constant_time_size<false>>;

	RealmSession &session;

	/** local id of this widget; must not be nullptr since widgets
	    without an id cannot have a session */
	const DString id;

	Set children;

	/** last relative URI */
	DString path_info;

	/** last query string */
	DString query_string;

	/**
	 * Throws std::bad_alloc on error.
	 */
	WidgetSession(RealmSession &_session, const char *_id);

	/**
	 * Throws std::bad_alloc on error.
	 */
	WidgetSession(struct dpool &pool, const WidgetSession &src,
		      RealmSession &_session);

	void Destroy(struct dpool &pool) noexcept;

	gcc_pure
	WidgetSession *GetChild(const char *child_id, bool create);
};

struct Session;

/**
 * A session associated with a user.
 */
struct RealmSession
	: boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

	static constexpr auto link_mode = boost::intrusive::normal_link;
	using LinkMode = boost::intrusive::link_mode<link_mode>;
	using SetHook = boost::intrusive::unordered_set_member_hook<LinkMode>;
	SetHook set_hook;

	Session &parent;

	struct Compare {
		bool operator()(const RealmSession &a, const RealmSession &b) const {
			return strcmp(a.realm, b.realm) < 0;
		}

		bool operator()(const RealmSession &a, const char *b) const {
			return strcmp(a.realm, b) < 0;
		}

		bool operator()(const char *a, const RealmSession &b) const {
			return strcmp(a, b.realm) < 0;
		}
	};

	/**
	 * The name of this session's realm.  It is always non-nullptr.
	 */
	const DString realm;

	/**
	 * The site name as provided by the translation server in the
	 * packet #TRANSLATE_SESSION_SITE.
	 */
	DString site;

	/** the user name which is logged in (nullptr if anonymous), provided
	    by the translation server */
	DString user;

	/** when will the #user attribute expire? */
	Expiry user_expires = Expiry::Never();

	/** a map of widget path to WidgetSession */
	WidgetSession::Set widgets;

	/** all cookies received by widget servers */
	CookieJar cookies;

	/**
	 * Throws std::bad_alloc on error.
	 */
	RealmSession(Session &_parent, const char *realm);

	/**
	 * Throws std::bad_alloc on error.
	 */
	RealmSession(Session &_parent, const RealmSession &src);

	void ClearSite() noexcept;
	bool SetSite(const char *_site);

	/**
	 * @param max_age 0 = expires immediately; negative = never
	 * expires.
	 */
	bool SetUser(const char *user, std::chrono::seconds max_age);
	void ClearUser() noexcept;

	void Expire(Expiry now) noexcept;

	gcc_pure
	WidgetSession *GetWidget(const char *widget_id, bool create);
};

struct Session {
	static constexpr auto link_mode = boost::intrusive::normal_link;
	using LinkMode = boost::intrusive::link_mode<link_mode>;
	using SetHook = boost::intrusive::unordered_set_member_hook<LinkMode>;
	SetHook set_hook;

	struct dpool &pool;

	/** identification number of this session */
	const SessionId id;

	/** a secret used to generate CSRF tokens */
	SessionId csrf_salt;

	/** when will this session expire? */
	Expiry expires;

	/**
	 * Counts how often this session has been used.
	 */
	unsigned counter = 1;

	/** is this a new session, i.e. there hasn't been a second request
	    yet? */
	bool is_new = true;

	/** has a HTTP cookie with this session id already been sent? */
	bool cookie_sent = false;

	/** has a HTTP cookie with this session id already been received? */
	bool cookie_received = false;

	/** an opaque string for the translation server */
	ConstBuffer<void> translate = nullptr;

	/** optional  for the "Accept-Language" header, provided
	    by the translation server */
	DString language;

	/** @see #TRANSLATE_EXTERNAL_SESSION_MANAGER */
	HttpAddress *external_manager = nullptr;

	/** @see #TRANSLATE_EXTERNAL_SESSION_KEEPALIVE */
	std::chrono::duration<uint16_t> external_keepalive;

	std::chrono::steady_clock::time_point next_external_keepalive;

	using RealmSessionSet =
		boost::intrusive::set<RealmSession,
				      boost::intrusive::compare<RealmSession::Compare>,
				      boost::intrusive::constant_time_size<false>>;

	RealmSessionSet realms;

	Session(struct dpool &_pool, SessionId _id);

	/**
	 * Throws std::bad_alloc on error.
	 */
	Session(struct dpool &_pool, const Session &src);

	void Destroy() noexcept;

	/**
	 * Calculates the score for purging the session: higher score
	 * means more likely to be purged.
	 */
	gcc_pure
	unsigned GetPurgeScore() const noexcept;

	gcc_pure
	bool HasUser() const noexcept {
		for (auto &realm : realms)
			if (realm.user != nullptr)
				return true;

		return false;
	}

	bool SetTranslate(ConstBuffer<void> translate);
	void ClearTranslate() noexcept;

	bool SetLanguage(const char *language);
	void ClearLanguage() noexcept;

	bool SetExternalManager(const HttpAddress &address,
				std::chrono::steady_clock::time_point now,
				std::chrono::duration<uint16_t> keepalive);

	void Expire(Expiry now) noexcept;

	gcc_pure
	RealmSession *GetRealm(const char *realm);

	struct Disposer {
		void operator()(Session *session) {
			session->Destroy();
		}
	};
};

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

static inline void
session_put(RealmSession &session) noexcept
{
	session_put(&session.parent);
}

static inline void
session_put(RealmSession *session) noexcept
{
	session_put(&session->parent);
}

inline RealmSession *
session_get_realm(SessionId id, const char *realm) noexcept
{
	auto *session = session_get(id);
	if (session == nullptr)
		return nullptr;

	auto *realm_session = session->GetRealm(realm);
	if (realm_session == nullptr)
		session_put(session);

	return realm_session;
}

/**
 * Deletes the session with the specified id.  The current process
 * must not hold a sssion lock.
 */
void
session_delete(SessionId id) noexcept;

class SessionLease {
	friend class RealmSessionLease;

	Session *session;

public:
	SessionLease() noexcept:session(nullptr) {}
	SessionLease(std::nullptr_t) noexcept:session(nullptr) {}

	explicit SessionLease(SessionId id) noexcept
		:session(session_get(id)) {}

	explicit SessionLease(Session *_session) noexcept
		:session(_session) {}

	SessionLease(SessionLease &&src) noexcept
		:session(src.session) {
		src.session = nullptr;
	}

	~SessionLease() noexcept {
		if (session != nullptr)
			session_put(session);
	}

	SessionLease &operator=(SessionLease &&src) noexcept {
		std::swap(session, src.session);
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
	RealmSession *session;

public:
	RealmSessionLease() noexcept:session(nullptr) {}
	RealmSessionLease(std::nullptr_t) noexcept:session(nullptr) {}

	RealmSessionLease(SessionLease &&src, const char *realm) noexcept
		:session(src.session != nullptr
			 ? src.session->GetRealm(realm)
			 : nullptr) {
		if (session != nullptr)
			src.session = nullptr;
	}

	RealmSessionLease(SessionId id, const char *realm) noexcept
		:session(nullptr) {
		SessionLease parent(id);
		if (!parent)
			return;

		session = parent.session->GetRealm(realm);
		if (session != nullptr)
			parent.session = nullptr;
	}

	explicit RealmSessionLease(RealmSession *_session) noexcept
		:session(_session) {}

	RealmSessionLease(RealmSessionLease &&src) noexcept
		:session(src.session) {
		src.session = nullptr;
	}

	~RealmSessionLease() noexcept {
		if (session != nullptr)
			session_put(&session->parent);
	}

	RealmSessionLease &operator=(RealmSessionLease &&src) noexcept {
		std::swap(session, src.session);
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
