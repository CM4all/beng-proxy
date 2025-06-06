// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Request.hxx"
#include "RLogger.hxx"
#include "Instance.hxx"
#include "Bot.hxx"
#include "session/Lease.hxx"
#include "session/Manager.hxx"
#include "session/Session.hxx"
#include "http/IncomingRequest.hxx"
#include "http/CommonHeaders.hxx"
#include "http/CookieExtract.hxx"
#include "util/djb_hash.hxx"
#include "util/HexFormat.hxx"
#include "util/SpanCast.hxx"
#include "util/StringSplit.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

[[gnu::pure]]
static std::string_view
ExtractCookieRaw(const StringMap &headers, std::string_view name) noexcept
{
	const auto r = headers.EqualRange(cookie_header);

	for (auto i = r.first; i != r.second; ++i) {
		const auto value = ExtractCookieRaw(i->value, name);
		if (value.data() != nullptr)
			return value;
	}

	return {};
}

inline SessionLease
Request::LoadSession(std::string_view _session_id) noexcept
{
	assert(!stateless);
	assert(!session_id.IsDefined());

	auto [sid, recover] = Split(_session_id, '/');

	if (!session_id.Parse(sid))
		return nullptr;

	const AllocatorPtr alloc(pool);

	auto session = GetSession();
	if (session) {
		if (session->translate.data() != nullptr)
			translate.request.session = alloc.Dup(std::span(session->translate));

		session->cookie_received = true;

		session->Expire(instance.event_loop.SteadyNow());
	} else {
		/* we have to duplicate the string because it needs to
		   be null-terminated */
		recover_session_from_cookie = alloc.DupZ(recover);
		// TODO: do we need to unescape the string?
	}

	return session;
}

static const char *
build_session_cookie_name(AllocatorPtr alloc, const BpConfig *config,
			  const StringMap &headers) noexcept
{
	if (!config->dynamic_session_cookie)
		return config->session_cookie.c_str();

	const char *host = headers.Get(host_header);
	if (host == nullptr || *host == 0)
		return config->session_cookie.c_str();

	size_t length = config->session_cookie.length();
	char *name = alloc.NewArray<char>(length + 5);
	char *p = std::copy(config->session_cookie.begin(),
			    config->session_cookie.end(),
			    name);
	p = HexFormatUint16Fixed(p, djb_hash_string(host));
	*p = 0;
	return name;
}

inline std::string_view
Request::GetCookieSessionId() noexcept
{
	assert(!stateless);
	assert(session_cookie != nullptr);

	return ExtractCookieRaw(request.headers, session_cookie);
}

void
Request::DetermineSession() noexcept
{
	const char *user_agent = request.headers.Get(user_agent_header);

	/* note: this method is called very early in the request handler,
	   and the "stateless" flag may later be updated by
	   MakeStateless() if the TranslateResponse suggests to do so */
	stateless = user_agent == nullptr || user_agent_is_bot(user_agent);
	if (stateless) {
		return;
	}

	session_cookie = build_session_cookie_name(pool,
						   &instance.config,
						   request.headers);

	const auto sid = GetCookieSessionId();
	if (sid.empty())
		return;

	LoadSession(sid);
}

SessionLease
Request::GetSession() const noexcept
{
	return {*instance.session_manager, session_id};
}

RealmSessionLease
Request::GetRealmSession() const noexcept
{
	assert(realm != nullptr);

	return {*instance.session_manager, session_id, realm};
}

SessionLease
Request::MakeSession() noexcept
{
	if (stateless)
		return nullptr;

	{
		auto lease = GetSession();
		if (lease)
			return lease;
	}

	auto &session_manager = *instance.session_manager;
	auto session = session_manager.CreateSession();
	assert(session);

	session_id = session->id;
	send_session_cookie = true;
	recover_session_to_cookie = nullptr;

	return session;
}

RealmSessionLease
Request::MakeRealmSession() noexcept
{
	assert(realm != nullptr);

	auto session = MakeSession();
	if (!session)
		return nullptr;

	return {std::move(session), realm};
}

void
Request::IgnoreSession() noexcept
{
	if (!session_id.IsDefined())
		return;

	assert(!stateless);

	session_id.Clear();
	send_session_cookie = false;
}

void
Request::DiscardSession() noexcept
{
	if (!session_id.IsDefined())
		return;

	assert(!stateless);

	instance.session_manager->EraseAndDispose(session_id);
	session_id.Clear();

	translate.request.session = {};
	send_session_cookie = false;
}

void
Request::DiscardRealmSession() noexcept
{
	if (!session_id.IsDefined())
		return;

	assert(!stateless);

	instance.session_manager->DiscardRealmSession(session_id, realm);
	session_id.Clear();

	translate.request.session = {};
	send_session_cookie = false;
}

/**
 * Determine the realm name, consider the override by the translation
 * server.  Guaranteed to return non-nullptr.
 */
static const char *
get_request_realm(AllocatorPtr alloc, const StringMap &request_headers,
		  const TranslateResponse &response,
		  std::span<const std::byte> auth_base) noexcept
{
	if (response.realm != nullptr)
		return response.realm;

	if (response.realm_from_auth_base) {
		assert(auth_base.data() != nullptr);
		// TODO: what if AUTH contains null bytes?
		return alloc.DupZ(ToStringView(auth_base));
	}

	const char *host = request_headers.Get(host_header);
	if (host != nullptr)
		return alloc.DupToLower(host);

	/* fall back to empty string as the default realm if there is no
	   "Host" header */
	return "";
}

void
Request::ApplyTranslateRealm(const TranslateResponse &response,
			     std::span<const std::byte> auth_base) noexcept
{
	if (realm != nullptr)
		/* was already called by Request::HandleAuth(), and no need to
		   check again */
		return;

	realm = get_request_realm(pool, request.headers, response, auth_base);
}

RealmSessionLease
Request::ApplyTranslateSession(const TranslateResponse &response) noexcept
{
	const AllocatorPtr alloc(pool);
	auto session = GetRealmSession();

	if (user == nullptr && session && session->user != nullptr)
		user = alloc.DupZ((std::string_view)session->user);

	if (response.attach_session.data() != nullptr &&
	    (!session || !session->parent.IsAttach(response.attach_session))) {
		session = instance.session_manager->Attach(std::move(session),
							   realm,
							   response.attach_session);
		if (session && session->parent.id != session_id) {
			/* if we have switched to a different session,
			   send a new session cookie */
			session_id = session->parent.id;
			send_session_cookie = true;
			recover_session_to_cookie = nullptr;
		}
	}

	if (response.session.data() != nullptr) {
		if (response.session.empty()) {
			/* clear translate session */

			if (session)
				session->parent.ClearTranslate();
		} else {
			/* set new translate session */

			if (!session)
				session = MakeRealmSession();

			if (session)
				session->parent.SetTranslate(response.session);
		}
	}

	if (response.realm_session.data() != nullptr) {
		if (response.realm_session.empty()) {
			/* clear translate session */

			if (session)
				session->ClearTranslate();
		} else {
			/* set new translate session */

			if (!session)
				session = MakeRealmSession();

			if (session)
				session->SetTranslate(response.realm_session);
		}
	}

	if (response.recover_session != nullptr) {
		if (!session)
			session = MakeRealmSession();

		if (session &&
		    session->parent.SetRecover(response.recover_session)) {
			send_session_cookie = true;
			recover_session_to_cookie = response.recover_session;
		}
	}

	if (response.session_site != nullptr) {
		if (*response.session_site == 0) {
			/* clear site */

			if (session)
				session->ClearSite();
		} else {
			/* set new site */

			if (!session)
				session = MakeRealmSession();

			if (session)
				session->SetSite(response.session_site);

			auto &rl = *(BpRequestLogger *)request.logger;
			rl.site_name = response.session_site;
		}
	} else if (session && session->site != nullptr) {
		auto &rl = *(BpRequestLogger *)request.logger;
		rl.site_name = alloc.DupZ((std::string_view)session->site);
	}

	if (response.user != nullptr) {
		if (*response.user == 0) {
			/* log out */

			user = nullptr;

			if (session)
				session->ClearUser();
		} else {
			/* log in */

			user = response.user;

			if (!session)
				session = MakeRealmSession();

			if (session)
				session->SetUser(response.user, response.user_max_age);
		}
	}

	if (response.language != nullptr) {
		if (*response.language == 0) {
			/* reset language setting */

			if (session)
				session->parent.ClearLanguage();
		} else {
			/* override language */

			if (!session)
				session = MakeRealmSession();

			if (session)
				session->parent.SetLanguage(response.language);
		}
	}

	if (response.external_session_manager != nullptr) {
		if (!session)
			session = MakeRealmSession();

		if (session)
			session->parent.SetExternalManager(*response.external_session_manager,
							   instance.event_loop.SteadyNow(),
							   response.external_session_keepalive);
	}

	/* apply SESSION_COOKIE_SAME_SITE; the setting was already
	   copied early from TranslateResponse by
	   OnTranslateResponse() */
	if (session_cookie_same_site != CookieSameSite::DEFAULT &&
	    session &&
	    session_cookie_same_site != session->session_cookie_same_site &&
	    !send_session_cookie) {
		/* cookie attribute "SameSite" has changed - we need
		   to resend the session cookie with the new attribute
		   value */
		send_session_cookie = true;

		/* this field needs to be initialized if
		   send_session_cookie is set */
		recover_session_to_cookie = nullptr;
	}

	return session;
}
