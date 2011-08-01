/*
 * The BENG request struct.  This is only used by the handlers
 * (handler.c, file-handler.c etc.).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.h"
#include "session.h"
#include "http-server.h"
#include "cookie-server.h"
#include "transformation.h"
#include "args.h"
#include "bot.h"
#include "dpool.h"

#include <daemon/log.h>

bool
request_processor_enabled(const struct request *request)
{
    const struct transformation *transformation;

    for (transformation = request->translate.response->views->transformation;
         transformation != NULL;
         transformation = transformation->next)
        if (transformation->type == TRANSFORMATION_PROCESS)
            return true;

    return false;
}

void
request_discard_body(struct request *request)
{
    if (request->request->body != NULL && !request->body_consumed) {
        request->body_consumed = true;
        istream_close_unused(request->request->body);
    }
}

void
request_args_parse(struct request *request)
{
    assert(request != NULL);
    assert(request->args == NULL);

    if (strref_is_empty(&request->uri.args)) {
        request->args = NULL;
        request->translate.request.param = NULL;
        request->translate.request.session = NULL;
        return;
    }

    request->args = args_parse(request->request->pool,
                               request->uri.args.data, request->uri.args.length);
    request->translate.request.param = strmap_remove(request->args, "translate");
    request->translate.request.session = NULL;
}

static const struct strmap *
request_get_cookies(struct request *request)
{
    const char *cookie;

    if (request->cookies != NULL)
        return request->cookies;

    cookie = strmap_get(request->request->headers, "cookie");
    if (cookie == NULL)
        return NULL;

    request->cookies = strmap_new(request->request->pool, 8);
    cookie_map_parse(request->cookies, cookie, request->request->pool);

    return request->cookies;
}

static struct session *
request_load_session(struct request *request, const char *session_id)
{
    struct session *session;

    assert(request != NULL);
    assert(!session_id_is_defined(request->session_id));
    assert(session_id != NULL);

    if (!session_id_parse(session_id, &request->session_id))
        return NULL;

    session = request_get_session(request);
    if (session == NULL)
        return NULL;

    if (session->translate != NULL)
        request->translate.request.session =
            p_strdup(request->request->pool, session->translate);

    if (!session->cookie_sent)
        request->send_session_cookie = true;

    session->new = false;

    return session;
}

static const char *
request_get_uri_session_id(const struct request *request)
{
    assert(request != NULL);

    return strmap_get_checked(request->args, "session");
}

static const char *
request_get_cookie_session_id(struct request *request)
{
    const struct strmap *cookies = request_get_cookies(request);

    return strmap_get_checked(cookies, "beng_proxy_session");
}

void
request_determine_session(struct request *request)
{
    const char *user_agent;
    const char *session_id;
    bool cookie_received = false;
    struct session *session;

    assert(request != NULL);

    request->session_realm = NULL;

    user_agent = strmap_get(request->request->headers, "user-agent");
    request->stateless = user_agent == NULL ||
        user_agent_is_bot(user_agent);
    if (request->stateless)
        return;

    session_id = request_get_uri_session_id(request);
    if (session_id == NULL || *session_id == 0) {
        session_id = request_get_cookie_session_id(request);
        if (session_id == NULL)
            return;

        cookie_received = true;
    }

    session = request_load_session(request, session_id);
    if (session == NULL) {
        if (!cookie_received && request->args != NULL)
            /* remove invalid session id from URI args */
            strmap_remove(request->args, "session");

        return;
    }

    if (!cookie_received) {
        const char *p = request_get_cookie_session_id(request);
        if (p != NULL && strcmp(p, session_id) == 0)
            cookie_received = true;
    }

    if (cookie_received) {
        session->cookie_received = true;

        if (request->args != NULL)
            /* we're using cookies, and we can safely remove the
               session id from the args */
            strmap_remove(request->args, "session");
    }

    request->session_realm = p_strdup(request->request->pool, session->realm);

    session_put(session);
}

struct session *
request_make_session(struct request *request)
{
    struct session *session;

    assert(request != NULL);

    if (request->stateless)
        return NULL;

    session = request_get_session(request);
    if (session != NULL)
        return session;

    session = session_new();
    if (session == NULL) {
        daemon_log(1, "Failed to allocate a session\n");
        return NULL;
    }

    session->realm = d_strdup(session->pool, request->realm);

    request->session_id = session->id;
    request->send_session_cookie = true;

    if (request->args == NULL)
        request->args = strmap_new(request->request->pool, 4);
    strmap_set(request->args, "session",
               session_id_format(request->session_id,
                                 &request->session_id_string));

    return session;
}

void
request_ignore_session(struct request *request)
{
    if (request->args != NULL)
        strmap_remove(request->args, "session");

    session_id_clear(&request->session_id);
}

void
request_discard_session(struct request *request)
{
    if (!session_id_is_defined(request->session_id))
        return;

    if (request->args != NULL)
        strmap_remove(request->args, "session");

    session_delete(request->session_id);
    session_id_clear(&request->session_id);
}
