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
#include "http/CookieJar.hxx"
#include "pool/Ptr.hxx"
#include "util/AllocatedArray.hxx"
#include "util/AllocatedString.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Expiry.hxx"

#include <boost/intrusive/set.hpp>
#include <boost/intrusive/unordered_set_hook.hpp>

#include <chrono>

#include <string.h>

struct RealmSession;
struct HttpAddress;

/**
 * Session data associated with a widget instance (struct widget).
 */
struct WidgetSession
	: boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

	struct Compare {
		bool operator()(const WidgetSession &a, const WidgetSession &b) const {
			return strcmp(a.id.c_str(), b.id.c_str()) < 0;
		}

		bool operator()(const WidgetSession &a, const char *b) const {
			return strcmp(a.id.c_str(), b) < 0;
		}

		bool operator()(const char *a, const WidgetSession &b) const {
			return strcmp(a, b.id.c_str()) < 0;
		}
	};

	using Set = boost::intrusive::set<WidgetSession,
					  boost::intrusive::compare<Compare>,
					  boost::intrusive::constant_time_size<false>>;

	/** local id of this widget; must not be nullptr since widgets
	    without an id cannot have a session */
	const AllocatedString id;

	Set children;

	/** last relative URI */
	AllocatedString path_info;

	/** last query string */
	AllocatedString query_string;

	template<typename I>
	explicit WidgetSession(I &&_id) noexcept
		:id(std::forward<I>(_id)) {}

	~WidgetSession() noexcept;

	static void Attach(Set &dest, Set &&src) noexcept;
	void Attach(WidgetSession &&other) noexcept;

	[[gnu::pure]]
	WidgetSession *GetChild(const char *child_id, bool create) noexcept;
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

	SetHook by_attach_hook;

	Session &parent;

	struct Compare {
		bool operator()(const RealmSession &a, const RealmSession &b) const {
			return strcmp(a.realm.c_str(), b.realm.c_str()) < 0;
		}

		bool operator()(const RealmSession &a, const char *b) const {
			return strcmp(a.realm.c_str(), b) < 0;
		}

		bool operator()(const char *a, const RealmSession &b) const {
			return strcmp(a, b.realm.c_str()) < 0;
		}
	};

	/**
	 * The name of this session's realm.  It is always non-nullptr.
	 */
	const AllocatedString realm;

	/**
	 * The site name as provided by the translation server in the
	 * packet #TRANSLATE_SESSION_SITE.
	 */
	AllocatedString site;

	/** the user name which is logged in (nullptr if anonymous), provided
	    by the translation server */
	AllocatedString user;

	/** when will the #user attribute expire? */
	Expiry user_expires = Expiry::Never();

	/** a map of widget path to WidgetSession */
	WidgetSession::Set widgets;

	/** all cookies received by widget servers */
	CookieJar cookies;

	template<typename R>
	RealmSession(Session &_parent, R &&_realm)
		:parent(_parent),
		 realm(std::forward<R>(_realm))
	{
	}

	RealmSession(Session &_parent, RealmSession &&src) noexcept
		:parent(_parent),
		 realm(src.realm),
		 site(std::move(src.site)),
		 user(std::move(src.user)),
		 user_expires(src.user_expires),
		 widgets(std::move(src.widgets)),
		 cookies(std::move(src.cookies))
	{
	}

	void Attach(RealmSession &&other) noexcept;

	void ClearSite() noexcept {
		site = nullptr;
	}

	void SetSite(const char *_site) noexcept {
		site = _site;
	}

	/**
	 * @param max_age 0 = expires immediately; negative = never
	 * expires.
	 */
	void SetUser(const char *user, std::chrono::seconds max_age) noexcept;
	void ClearUser() noexcept {
		user = nullptr;
	}

	void Expire(Expiry now) noexcept;

	[[gnu::pure]]
	WidgetSession *GetWidget(const char *widget_id, bool create) noexcept;
};

struct Session {
	static constexpr auto link_mode = boost::intrusive::normal_link;
	using LinkMode = boost::intrusive::link_mode<link_mode>;
	using SetHook = boost::intrusive::unordered_set_member_hook<LinkMode>;
	SetHook set_hook;

	using ByAttachHook = boost::intrusive::unordered_set_member_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>;
	ByAttachHook by_attach_hook;

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
	AllocatedArray<std::byte> translate;

	/** an opaque string for attaching sessions; if this is set,
	    then the session is in
	    SessionContainer::sessions_by_attach */
	AllocatedArray<std::byte> attach;

	/** optional  for the "Accept-Language" header, provided
	    by the translation server */
	AllocatedString language;

	PoolPtr external_manager_pool;

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

	explicit Session(SessionId _id) noexcept;
	~Session() noexcept;

	/**
	 * Calculates the score for purging the session: higher score
	 * means more likely to be purged.
	 */
	[[gnu::pure]]
	unsigned GetPurgeScore() const noexcept;

	[[gnu::pure]]
	bool HasUser() const noexcept {
		for (auto &realm : realms)
			if (realm.user != nullptr)
				return true;

		return false;
	}

	void SetTranslate(ConstBuffer<void> translate);
	void ClearTranslate() noexcept;

	/**
	 * Does this session have the specified "attach" value?
	 */
	[[gnu::pure]]
	bool IsAttach(ConstBuffer<std::byte> other) const noexcept;

	void Attach(Session &&other) noexcept;

	void SetLanguage(const char *_language) {
		language = _language;
	}

	void ClearLanguage() noexcept {
		language = nullptr;
	}

	void SetExternalManager(const HttpAddress &address,
				std::chrono::steady_clock::time_point now,
				std::chrono::duration<uint16_t> keepalive) noexcept;

	void Expire(Expiry now) noexcept;

	[[gnu::pure]]
	RealmSession *GetRealm(const char *realm) noexcept;
};

/**
 * Deletes the session with the specified id.  The current process
 * must not hold a sssion lock.
 */
void
session_delete(SessionId id) noexcept;
