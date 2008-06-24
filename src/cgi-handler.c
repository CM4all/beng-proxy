/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "request.h"
#include "http-server.h"
#include "cgi.h"

void
cgi_handler(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    const char *script_name, *query_string, *document_root;

    pool_ref(request->pool);

    query_string = strchr(request->uri, '?');
    if (query_string == NULL) {
        script_name = request->uri;
        query_string = "";
    } else {
        script_name = p_strndup(request->pool, request->uri,
                                query_string - request->uri);
        ++query_string;
    }

    document_root = tr->document_root;
    if (document_root == NULL)
        document_root = "/var/www";

    cgi_new(request->pool, tr->address.u.cgi.jail,
            tr->address.u.cgi.interpreter, tr->address.u.cgi.action,
            tr->address.u.cgi.path,
            request->method, request->uri,
            script_name, tr->address.u.cgi.path_info,
            query_string, document_root,
            request->headers, request->body,
            &response_handler, request2,
            request2->async_ref);
}
