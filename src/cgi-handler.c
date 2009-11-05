/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "request.h"
#include "request-forward.h"
#include "http-server.h"
#include "cgi.h"

void
cgi_handler(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    struct forward_request forward;
    const char *query_string;

    request_forward(&forward, request2,
                    &tr->request_header_forward,
                    NULL, NULL);

    query_string = strchr(request->uri, '?');
    if (query_string == NULL)
        query_string = "";
    else
        ++query_string;

    cgi_new(request->pool, tr->address.u.cgi.jail,
            tr->address.u.cgi.interpreter, tr->address.u.cgi.action,
            tr->address.u.cgi.path,
            forward.method, request->uri,
            tr->address.u.cgi.script_name, tr->address.u.cgi.path_info,
            query_string, tr->address.u.cgi.document_root,
            forward.headers, forward.body,
            &response_handler, request2,
            request2->async_ref);
}
