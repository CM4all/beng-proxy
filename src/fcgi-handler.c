/*
 * Run a FastCGI program.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "request.h"
#include "http-server.h"
#include "fcgi-request.h"
#include "global.h"

void
fcgi_handler(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    const char *query_string, *document_root;

    assert(!request2->body_consumed);

    request2->body_consumed = true;

    query_string = strchr(request->uri, '?');
    if (query_string == NULL)
        query_string = "";
    else
        ++query_string;

    document_root = tr->document_root;
    if (document_root == NULL)
        document_root = "/var/www";

    fcgi_request(request->pool, global_fcgi_stock, global_tcp_stock,
                 tr->address.u.cgi.path,
                 request->method, request->uri,
                 tr->address.u.cgi.script_name, tr->address.u.cgi.path_info,
                 query_string, tr->address.u.cgi.document_root,
                 request->headers, request->body,
                 &response_handler, request2,
                 request2->async_ref);
}
