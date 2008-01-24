/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "request.h"
#include "http-server.h"
#include "cgi.h"

/** remove path_info from uri to get script_name */
static const char *
remove_tail(pool_t pool, const char *p, const char *tail)
{
    size_t p_length = strlen(p);
    size_t tail_length = strlen(tail);

    if (p_length >= tail_length &&
        memcmp(p + p_length - tail_length, tail, tail_length) == 0)
        return p_strndup(pool, p, p_length - tail_length);
    else
        return p;
}

void
cgi_handler(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    const char *script_name, *path_info, *query_string, *document_root;

    pool_ref(request->pool);

    async_ref_clear(&request2->url_stream);

    query_string = strchr(request->uri, '?');
    if (query_string == NULL) {
        script_name = request->uri;
        query_string = "";
    } else {
        script_name = p_strndup(request->pool, request->uri,
                                query_string - request->uri);
        ++query_string;
    }

    path_info = tr->path_info;
    if (path_info == NULL)
        path_info = "";
    else
        script_name = remove_tail(request->pool, script_name, path_info);

    document_root = tr->document_root;
    if (document_root == NULL)
        document_root = "/var/www";

    cgi_new(request->pool, tr->path,
            request->method, request->uri,
            script_name, path_info, query_string, document_root,
            request->headers, request->body,
            &response_handler, request2,
            &request2->url_stream);
}
