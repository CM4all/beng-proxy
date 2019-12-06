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

#include "Request.hxx"
#include "Connection.hxx"
#include "Instance.hxx"
#include "session/Session.hxx"
#include "session/Glue.hxx"
#include "http/IncomingRequest.hxx"
#include "http/CookieServer.hxx"
#include "bot.h"
#include "pool/pbuffer.hxx"
#include "strmap.hxx"
#include "util/HexFormat.h"
#include "util/djbhash.h"

inline const StringMap *
Request::GetCookies()
{
    if (cookies != nullptr)
        return cookies;

    const char *cookie = request.headers.Get("cookie");
    if (cookie == nullptr)
        return nullptr;

    return cookies = NewFromPool<StringMap>(pool,
                                            cookie_map_parse(pool, cookie));
}

inline SessionLease
Request::LoadSession(const char *_session_id)
{
    assert(!stateless);
    assert(!session_id.IsDefined());
    assert(_session_id != nullptr);

    if (!session_id.Parse(_session_id))
        return nullptr;

    auto session = GetSession();
    if (session) {
        if (!session->translate.IsNull())
            translate.request.session = DupBuffer(pool, session->translate);

        if (!session->cookie_sent)
            send_session_cookie = true;

        session->is_new = false;

        session->Expire(instance.event_loop.SteadyNow());
    }

    return session;
}

static const char *
build_session_cookie_name(struct pool *pool, const BpConfig *config,
                          const StringMap &headers)
{
    if (!config->dynamic_session_cookie)
        return config->session_cookie.c_str();

    const char *host = headers.Get("host");
    if (host == nullptr || *host == 0)
        return config->session_cookie.c_str();

    size_t length = config->session_cookie.length();
    char *name = PoolAlloc<char>(*pool, length + 5);
    memcpy(name, config->session_cookie.data(), length);
    format_uint16_hex_fixed(name + length, djb_hash_string(host));
    name[length + 4] = 0;
    return name;
}

inline const char *
Request::GetCookieSessionId()
{
    assert(!stateless);
    assert(session_cookie != nullptr);

    return strmap_get_checked(GetCookies(), session_cookie);
}

void
Request::DetermineSession()
{
    const char *user_agent = request.headers.Get("user-agent");

    /* note: this method is called very early in the request handler,
       and the "stateless" flag may later be updated by
       MakeStateless() if the TranslateResponse suggests to do so */
    stateless = user_agent == nullptr || user_agent_is_bot(user_agent);
    if (stateless) {
        return;
    }

    session_cookie = build_session_cookie_name(&pool,
                                               &instance.config,
                                               request.headers);

    bool cookie_received = false;
    const char *sid = GetCookieSessionId();
    if (sid == nullptr)
        return;

    cookie_received = true;

    auto session = LoadSession(sid);
    if (!session) {
        return;
    }

    if (!cookie_received) {
        const char *p = GetCookieSessionId();
        if (p != nullptr && strcmp(p, sid) == 0)
            cookie_received = true;
    }

    if (cookie_received) {
        session->cookie_received = true;
    }
}

RealmSessionLease
Request::GetRealmSession() const
{
    assert(realm != nullptr);

    return {session_id, realm};
}

SessionLease
Request::MakeSession()
{
    if (stateless)
        return nullptr;

    {
        auto lease = GetSession();
        if (lease)
            return lease;
    }

    auto *session = session_new();
    if (session == nullptr) {
        logger(1, "Failed to allocate a session");
        return nullptr;
    }

    session_id = session->id;
    send_session_cookie = true;

    return SessionLease(session);
}

RealmSessionLease
Request::MakeRealmSession()
{
    assert(realm != nullptr);

    auto session = MakeSession();
    if (!session)
        return nullptr;

    return {std::move(session), realm};
}

void
Request::IgnoreSession()
{
    if (!session_id.IsDefined())
        return;

    assert(!stateless);

    session_id.Clear();
    send_session_cookie = false;
}

void
Request::DiscardSession()
{
    if (!session_id.IsDefined())
        return;

    assert(!stateless);

    session_delete(session_id);
    session_id.Clear();
    send_session_cookie = false;
}

/**
 * Determine the realm name, consider the override by the translation
 * server.  Guaranteed to return non-nullptr.
 */
static const char *
get_request_realm(struct pool *pool, const StringMap &request_headers,
                  const TranslateResponse &response,
                  ConstBuffer<void> auth_base)
{
    if (response.realm != nullptr)
        return response.realm;

    if (response.realm_from_auth_base) {
        assert(!auth_base.IsNull());
        // TODO: what if AUTH contains null bytes?
        return p_strndup(pool, (const char *)auth_base.data, auth_base.size);
    }

    const char *host = request_headers.Get("host");
    if (host != nullptr)
        return p_strdup_lower(pool, host);

    /* fall back to empty string as the default realm if there is no
       "Host" header */
    return "";
}

void
Request::ApplyTranslateRealm(const TranslateResponse &response,
                             ConstBuffer<void> auth_base)
{
    if (realm != nullptr)
        /* was already called by Request::HandleAuth(), and no need to
           check again */
        return;

    realm = get_request_realm(&pool, request.headers, response, auth_base);
}

RealmSessionLease
Request::ApplyTranslateSession(const TranslateResponse &response)
{
    auto session = GetRealmSession();

    if (!response.session.IsNull()) {
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

            connection.per_request.site_name = response.session_site;
        }
    } else if (session && session->site != nullptr)
        connection.per_request.site_name = p_strdup(&pool, session->site);

    if (response.user != nullptr) {
        if (*response.user == 0) {
            /* log out */

            if (session)
                session->ClearUser();
        } else {
            /* log in */

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

    return session;
}
