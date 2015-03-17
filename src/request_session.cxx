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
#include "http_server.hxx"
#include "cookie_server.hxx"
#include "bot.h"
#include "dpool.h"
#include "pbuffer.hxx"
#include "strmap.hxx"
#include "crc.h"
#include "format.h"
#include "expiry.h"
#include "strutil.h"

#include <daemon/log.h>

static const struct strmap *
request_get_cookies(struct request &request)
{
    if (request.cookies != nullptr)
        return request.cookies;

    const char *cookie = request.request->headers->Get("cookie");
    if (cookie == nullptr)
        return nullptr;

    request.cookies = strmap_new(request.request->pool);
    cookie_map_parse(request.cookies, cookie, request.request->pool);

    return request.cookies;
}

static Session *
request_load_session(struct request &request, const char *session_id)
{
    assert(!request.stateless);
    assert(!session_id_is_defined(request.session_id));
    assert(session_id != nullptr);

    if (!session_id_parse(session_id, &request.session_id))
        return nullptr;

    auto *session = request_get_session(request);
    if (session == nullptr)
        return nullptr;

    if (!session->translate.IsNull())
        request.translate.request.session = DupBuffer(request.request->pool,
                                                      session->translate);

    if (!session->cookie_sent)
        request.send_session_cookie = true;

    session->is_new = false;

    return session;
}

static const char *
build_session_cookie_name(struct pool *pool, const struct config *config,
                          const struct strmap *headers)
{
    if (headers == nullptr || !config->dynamic_session_cookie)
        return config->session_cookie;

    const char *host = headers->Get("host");
    if (host == nullptr || *host == 0)
        return config->session_cookie;

    size_t length = strlen(config->session_cookie);
    char *name = PoolAlloc<char>(*pool, length + 5);
    memcpy(name, config->session_cookie, length);
    format_uint16_hex_fixed(name + length, crc16_string(0, host));
    name[length + 4] = 0;
    return name;
}

static const char *
request_get_uri_session_id(const struct request &request)
{
    assert(!request.stateless);

    return strmap_get_checked(request.args, "session");
}

static const char *
request_get_cookie_session_id(struct request &request)
{
    assert(!request.stateless);
    assert(request.session_cookie != nullptr);

    const auto *cookies = request_get_cookies(request);

    return strmap_get_checked(cookies, request.session_cookie);
}

void
request_determine_session(struct request &request)
{
    request.session_realm = nullptr;

    const char *user_agent = request.request->headers->Get("user-agent");
    request.stateless = user_agent == nullptr ||
        user_agent_is_bot(user_agent);
    if (request.stateless)
        return;

    request.session_cookie =
        build_session_cookie_name(request.request->pool,
                                  &request.connection->instance->config,
                                  request.request->headers);

    const char *session_id = request_get_uri_session_id(request);
    bool cookie_received = false;
    if (session_id == nullptr || *session_id == 0) {
        session_id = request_get_cookie_session_id(request);
        if (session_id == nullptr)
            return;

        cookie_received = true;
    }

    auto *session = request_load_session(request, session_id);
    if (session == nullptr) {
        if (!cookie_received && request.args != nullptr)
            /* remove invalid session id from URI args */
            request.args->Remove("session");

        return;
    }

    if (!cookie_received) {
        const char *p = request_get_cookie_session_id(request);
        if (p != nullptr && strcmp(p, session_id) == 0)
            cookie_received = true;
    }

    if (cookie_received) {
        session->cookie_received = true;

        if (request.args != nullptr)
            /* we're using cookies, and we can safely remove the
               session id from the args */
            request.args->Remove("session");
    }

    request.session_realm = p_strdup(request.request->pool, session->realm);

    session_put(session);
}

Session *
request_make_session(struct request &request)
{
    if (request.stateless)
        return nullptr;

    auto *session = request_get_session(request);
    if (session != nullptr)
        return session;

    session = session_new();
    if (session == nullptr) {
        daemon_log(1, "Failed to allocate a session\n");
        return nullptr;
    }

    session->realm = d_strdup(session->pool, request.realm);

    request.session_id = session->id;
    request.send_session_cookie = true;

    if (request.args == nullptr)
        request.args = strmap_new(request.request->pool);
    request.args->Set("session",
                      session_id_format(request.session_id,
                                        &request.session_id_string));

    return session;
}

void
request_ignore_session(struct request &request)
{
    if (!session_id_is_defined(request.session_id))
        return;

    assert(!request.stateless);

    if (request.args != nullptr)
        request.args->Remove("session");

    session_id_clear(&request.session_id);
}

void
request_discard_session(struct request &request)
{
    if (!session_id_is_defined(request.session_id))
        return;

    assert(!request.stateless);

    if (request.args != nullptr)
        request.args->Remove("session");

    session_delete(request.session_id);
    session_id_clear(&request.session_id);
}

/**
 * Determine the realm name, consider the override by the translation
 * server.  Guaranteed to return non-nullptr.
 */
static const char *
get_request_realm(struct pool *pool, const struct strmap *request_headers,
                  const TranslateResponse &response)
{
    if (response.realm != nullptr)
        return response.realm;

    const char *host = strmap_get_checked(request_headers, "host");
    if (host != nullptr) {
        char *p = p_strdup(pool, host);
        str_to_lower(p);
        return p;
    }

    /* fall back to empty string as the default realm if there is no
       "Host" header */
    return "";
}

void
request::ApplyTranslateRealm(const TranslateResponse &response)
{
    realm = get_request_realm(request->pool, request->headers, response);

    if (session_realm != nullptr && strcmp(realm, session_realm) != 0) {
        daemon_log(2, "ignoring spoofed session id from another realm (request='%s', session='%s')\n",
                   realm, session_realm);
        request_ignore_session(*this);
    }
}

Session *
request::ApplyTranslateSession(const TranslateResponse &response)
{
    if (response.session.IsNull() && response.user == nullptr &&
        response.language == nullptr)
        return nullptr;

    auto *session = request_get_session(*this);

    if (!response.session.IsNull()) {
        if (response.session.IsEmpty()) {
            /* clear translate session */

            if (session != nullptr)
                session_clear_translate(session);
        } else {
            /* set new translate session */

            if (session == nullptr)
                session = request_make_session(*this);

            if (session != nullptr)
                session_set_translate(session, response.session);
        }
    }

    if (response.user != nullptr) {
        if (*response.user == 0) {
            /* log out */

            if (session != nullptr)
                session_clear_user(session);
        } else {
            /* log in */

            if (session == nullptr)
                session = request_make_session(*this);

            if (session != nullptr)
                session_set_user(session, response.user,
                                 response.user_max_age);
        }
    } else if (session != nullptr && session->user != nullptr &&
               session->user_expires > 0 &&
               is_expired(session->user_expires)) {
        daemon_log(4, "user '%s' has expired\n", session->user);
        d_free(session->pool, session->user);
        session->user = nullptr;
    }

    if (response.language != nullptr) {
        if (*response.language == 0) {
            /* reset language setting */

            if (session != nullptr)
                session_clear_language(session);
        } else {
            /* override language */

            if (session == nullptr)
                session = request_make_session(*this);

            if (session != nullptr)
                session_set_language(session, response.language);
        }
    }

    return session;
}
