/*
 * The BENG request struct.  This is only used by the handlers
 * (handler.c, file-handler.c etc.).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.h"
#include "session.h"

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

struct session *
request_make_session(struct request *request)
{
    assert(request != NULL);

    if (request->session != NULL)
        return request->session;

    request->session = session_new();
    session_id_format(request->session_id_buffer, request->session->id);
    strmap_put(request->args, "session", request->session_id_buffer, 1);

    return request->session;
}
