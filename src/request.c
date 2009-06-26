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

bool
request_processor_enabled(struct request *request)
{
    const struct transformation *transformation;

    for (transformation = request->translate.response->views->transformation;
         transformation != NULL;
         transformation = transformation->next)
        if (transformation->type == TRANSFORMATION_PROCESS)
            return true;

    return false;
}

bool
response_dispatcher_wants_body(struct request *request)
{
    assert(http_server_request_has_body(request->request));
    assert(!request->body_consumed);

    return request->request->method == HTTP_METHOD_POST &&
        request_processor_enabled(request);
}

void
request_discard_body(struct request *request)
{
    if (request->request->body != NULL && !request->body_consumed)
        istream_close(request->request->body);
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

static struct strmap *
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
request_get_session(struct request *request, const char *session_id)
{
    struct session *session;

    assert(request != NULL);
    assert(request->session_id == 0);
    assert(session_id != NULL);

    request->session_id = session_id_parse(session_id);
    if (request->session_id == 0)
        return NULL;

    session = session_get(request->session_id);
    if (session != NULL && session->translate != NULL)
        request->translate.request.session =
            p_strdup(request->request->pool, session->translate);

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
    struct strmap *cookies = request_get_cookies(request);

    return strmap_get_checked(cookies, "beng_proxy_session");
}

void
request_determine_session(struct request *request)
{
    const char *session_id;
    bool cookie_received = false;
    struct session *session;

    assert(request != NULL);

    session_id = request_get_uri_session_id(request);
    if (session_id == NULL || *session_id == 0) {
        session_id = request_get_cookie_session_id(request);
        if (session_id == NULL)
            return;

        cookie_received = true;
    }

    session = request_get_session(request, session_id);
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
}

struct session *
request_make_session(struct request *request)
{
    struct session *session;

    assert(request != NULL);

    if (request->session_id != 0) {
        session = session_get(request->session_id);
        if (session != NULL)
            return session;
    }

    session = session_new();
    request->session_id = session->id;
    session_id_format(request->session_id_buffer, request->session_id);

    if (request->args == NULL)
        request->args = strmap_new(request->request->pool, 4);
    strmap_set(request->args, "session", request->session_id_buffer);

    return session;
}
