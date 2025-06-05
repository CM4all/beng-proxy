// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Session management.
 */

#pragma once

#include "Id.hxx"
#include "http/CookieJar.hxx"
#include "http/CookieSameSite.hxx"
#include "pool/Ptr.hxx"
#include "time/Expiry.hxx"
#include "util/AllocatedArray.hxx"
#include "util/AllocatedString.hxx"
#include "util/IntrusiveHashSet.hxx"

#include <chrono>
#include <map>
#include <string>

struct RealmSession;
struct HttpAddress;

/**
 * Session data associated with a widget instance (struct widget).
 */
struct WidgetSession {
	using Set = std::map<std::string, WidgetSession, std::less<>>;

	Set children;

	/** last relative URI */
	AllocatedString path_info;

	/** last query string */
	AllocatedString query_string;

	WidgetSession() noexcept = default;

	~WidgetSession() noexcept;

	WidgetSession(WidgetSession &&) noexcept = default;
	WidgetSession &operator=(WidgetSession &&) noexcept = default;

	static void Attach(Set &dest, Set &&src) noexcept;
	void Attach(WidgetSession &&other) noexcept;

	[[gnu::pure]]
	WidgetSession *GetChild(std::string_view child_id, bool create) noexcept;
};

struct Session;

/**
 * A session associated with a user.
 */
struct RealmSession {
	Session &parent;

	/**
	 * The site name as provided by the translation server in the
	 * packet #TRANSLATE_SESSION_SITE.
	 */
	AllocatedString site;

	/**
	 * An opaque string for the translation server obtained from
	 * TranslationCommand::REALM_SESSION.
	 */
	AllocatedArray<std::byte> translate;

	/** the user name which is logged in (nullptr if anonymous), provided
	    by the translation server */
	AllocatedString user;

	/** when will the #user attribute expire? */
	Expiry user_expires = Expiry::Never();

	/** a map of widget path to WidgetSession */
	WidgetSession::Set widgets;

	/** all cookies received by widget servers */
	CookieJar cookies;

	/**
	 * The "SameSite" attribute of the session cookie which was
	 * most recently sent to the client.
	 */
	CookieSameSite session_cookie_same_site = CookieSameSite::DEFAULT;

	explicit RealmSession(Session &_parent) noexcept
		:parent(_parent)
	{
	}

	RealmSession(Session &_parent, RealmSession &&src) noexcept
		:parent(_parent),
		 site(std::move(src.site)),
		 user(std::move(src.user)),
		 user_expires(src.user_expires),
		 widgets(std::move(src.widgets)),
		 cookies(std::move(src.cookies))
	{
	}

	RealmSession(RealmSession &&) noexcept = default;

	~RealmSession() noexcept;

	void Attach(RealmSession &&other) noexcept;

	void ClearSite() noexcept {
		site = nullptr;
	}

	void SetSite(const char *_site) noexcept {
		site = _site;
	}

	void SetTranslate(std::span<const std::byte> translate) noexcept;

	void ClearTranslate() noexcept {
		translate = nullptr;
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
	WidgetSession *GetWidget(std::string_view widget_id, bool create) noexcept;
};

struct Session {
	IntrusiveHashSetHook<IntrusiveHookMode::NORMAL> set_hook;

	IntrusiveHashSetHook<IntrusiveHookMode::AUTO_UNLINK> by_attach_hook;

	/** identification number of this session */
	const SessionId id;

	/** a secret used to generate CSRF tokens */
	const SessionId csrf_salt;

	/** when will this session expire? */
	Expiry expires;

	/**
	 * Counts how often this session has been used.
	 */
	unsigned counter = 1;

	/** has a HTTP cookie with this session id already been received? */
	bool cookie_received = false;

	/**
	 * An opaque string for the translation server obtained from
	 * TranslationCommand::SESSION.
	 */
	AllocatedArray<std::byte> translate;

	AllocatedString recover;

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

	using RealmSessionSet = std::map<std::string, RealmSession, std::less<>>;

	RealmSessionSet realms;

	Session(SessionId _id, SessionId _csrf_salt) noexcept;
	~Session() noexcept;

	/**
	 * Calculates the score for purging the session: higher score
	 * means more likely to be purged.
	 */
	[[gnu::pure]]
	unsigned GetPurgeScore() const noexcept;

	[[gnu::pure]]
	bool HasUser() const noexcept {
		for (auto &[name, realm] : realms)
			if (realm.user != nullptr)
				return true;

		return false;
	}

	void SetTranslate(std::span<const std::byte> translate) noexcept;
	void ClearTranslate() noexcept;

	/**
	 * @return true if the value modified
	 */
	bool SetRecover(const char *_recover) noexcept;

	/**
	 * Does this session have the specified "attach" value?
	 */
	[[gnu::pure]]
	bool IsAttach(std::span<const std::byte> other) const noexcept;

	void Attach(Session &&other) noexcept;

	void SetLanguage(const char *_language) noexcept {
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
	RealmSession *GetRealm(std::string_view realm) noexcept;

	bool DiscardRealm(std::string_view realm) noexcept;
};

/**
 * Deletes the session with the specified id.  The current process
 * must not hold a sssion lock.
 */
void
session_delete(SessionId id) noexcept;
