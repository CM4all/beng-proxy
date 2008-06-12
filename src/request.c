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

bool
request_processor_enabled(struct request *request)
{
    const struct translate_transformation *transformation;

    for (transformation = request->translate.response->transformation;
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

void
request_get_session(struct request *request, const char *session_id)
{
    struct session *session;

    assert(request != NULL);
    assert(request->session_id == 0);
    assert(session_id != NULL);

    request->session_id = session_id_parse(session_id);
    if (request->session_id == 0)
        return;

    session = session_get(request->session_id);
    if (session != NULL && session->translate != NULL)
        request->translate.request.session =
            p_strdup(request->request->pool, session->translate);
}

session_id_t
request_get_cookie_session_id(struct request *request)
{
    struct strmap *cookies = request_get_cookies(request);
    const char *session_id;

    if (cookies == NULL)
        return 0;

    session_id = strmap_get(cookies, "beng_proxy_session");
    if (session_id == NULL)
        return 0;

    return session_id_parse(session_id);
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
    request->session_id = session->uri_id;
    session_id_format(request->session_id_buffer, request->session_id);

    if (request->args == NULL)
        request->args = strmap_new(request->request->pool, 4);
    strmap_set(request->args, "session", request->session_id_buffer);

    return session;
}
