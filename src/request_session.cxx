/*
 * The BENG request struct.  This is only used by the handlers
 * (handler.c, file-handler.c etc.).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.hxx"
#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "session.hxx"
#include "session_manager.hxx"
#include "http_server/Request.hxx"
#include "cookie_server.hxx"
#include "bot.h"
#include "pbuffer.hxx"
#include "strmap.hxx"
#include "util/djbhash.h"
#include "format.h"

#include <daemon/log.h>

static const StringMap *
request_get_cookies(Request &request)
{
    if (request.cookies != nullptr)
        return request.cookies;

    const char *cookie = request.request.headers.Get("cookie");
    if (cookie == nullptr)
        return nullptr;

    request.cookies = NewFromPool<StringMap>(request.pool,
                                             cookie_map_parse(request.pool,
                                                              cookie));

    return request.cookies;
}

static SessionLease
request_load_session(Request &request, const char *session_id)
{
    assert(!request.stateless);
    assert(!request.session_id.IsDefined());
    assert(session_id != nullptr);

    if (!request.session_id.Parse(session_id))
        return nullptr;

    auto session = request.GetSession();
    if (session) {
        if (!session->translate.IsNull())
            request.translate.request.session = DupBuffer(request.pool,
                                                          session->translate);

        if (!session->cookie_sent)
            request.send_session_cookie = true;

        session->is_new = false;

        session->Expire(Expiry::Now());
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

static const char *
request_get_uri_session_id(const Request &request)
{
    assert(!request.stateless);

    return strmap_get_checked(request.args, "session");
}

static const char *
request_get_cookie_session_id(Request &request)
{
    assert(!request.stateless);
    assert(request.session_cookie != nullptr);

    const auto *cookies = request_get_cookies(request);

    return strmap_get_checked(cookies, request.session_cookie);
}

void
Request::DetermineSession()
{
    const char *user_agent = request.headers.Get("user-agent");
    stateless = user_agent == nullptr || user_agent_is_bot(user_agent);
    if (stateless) {
        /* don't propagate a stale session id to processed URIs */
        if (args != nullptr)
            args->Remove("session");
        return;
    }

    session_cookie = build_session_cookie_name(&pool,
                                               &instance.config,
                                               request.headers);

    const char *sid = request_get_uri_session_id(*this);
    bool cookie_received = false;
    if (sid == nullptr || *sid == 0) {
        sid = request_get_cookie_session_id(*this);
        if (sid == nullptr)
            return;

        cookie_received = true;
    }

    auto session = request_load_session(*this, sid);
    if (!session) {
        if (!cookie_received && args != nullptr)
            /* remove invalid session id from URI args */
            args->Remove("session");

        return;
    }

    if (!cookie_received) {
        const char *p = request_get_cookie_session_id(*this);
        if (p != nullptr && strcmp(p, sid) == 0)
            cookie_received = true;
    }

    if (cookie_received) {
        session->cookie_received = true;

        if (args != nullptr)
            /* we're using cookies, and we can safely remove the
               session id from the args */
            args->Remove("session");
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
        daemon_log(1, "Failed to allocate a session\n");
        return nullptr;
    }

    session_id = session->id;
    send_session_cookie = true;

    if (args == nullptr)
        args = strmap_new(&pool);
    args->Set("session", session_id.Format(session_id_string));

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

    if (args != nullptr)
        args->Remove("session");

    session_id.Clear();
    send_session_cookie = false;
}

void
Request::DiscardSession()
{
    if (!session_id.IsDefined())
        return;

    assert(!stateless);

    if (args != nullptr)
        args->Remove("session");

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
        if (response.session.IsEmpty()) {
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

            connection.site_name = response.session_site;
        }
    } else if (session && session->site != nullptr)
        connection.site_name = p_strdup(&pool, session->site);

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
                                               response.external_session_keepalive);
    }

    return session;
}
