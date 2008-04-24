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

int
request_processor_enabled(struct request *request)
{
    const struct translate_transformation *transformation;

    for (transformation = request->translate.response->transformation;
         transformation != NULL;
         transformation = transformation->next)
        if (transformation->type == TRANSFORMATION_PROCESS)
            return 1;

    return 0;
}

int
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
    session_id_t session_id2;

    assert(request != NULL);
    assert(request->session == NULL);
    assert(session_id != NULL);

    session_id2 = session_id_parse(session_id);
    if (session_id2 == 0)
        return;

    request->session = session_get(session_id2);

    if (request->session != NULL)
        request->translate.request.session = request->session->translate;
}

void
request_get_cookie_session(struct request *request)
{
    struct strmap *cookies = request_get_cookies(request);
    const char *session_id;

    assert(request->session == NULL);

    if (cookies == NULL)
        return;

    session_id = strmap_get(cookies, "beng_proxy_session");
    if (session_id == NULL)
        return;

    request_get_session(request, session_id);
    if (request->session != NULL)
        request->session->cookie_received = true;
}

struct session *
request_make_session(struct request *request)
{
    assert(request != NULL);

    if (request->session != NULL)
        return request->session;

    request->session = session_new();
    session_id_format(request->session_id_buffer, request->session->id);

    if (request->args == NULL)
        request->args = strmap_new(request->request->pool, 4);
    strmap_put(request->args, "session", request->session_id_buffer, 1);

    return request->session;
}
