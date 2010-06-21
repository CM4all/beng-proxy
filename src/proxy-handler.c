/*
 * Serve HTTP requests from another HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "request.h"
#include "request-forward.h"
#include "http-server.h"
#include "http-cache.h"
#include "uri-address.h"
#include "global.h"
#include "cookie-client.h"
#include "uri-extract.h"

static void
proxy_collect_cookies(struct request *request2, const struct strmap *headers,
                      const char *uri)
{
    const char *key = "set-cookie2";
    const char *cookies, *host_and_port;
    struct session *session;

    if (headers == NULL)
        return;

    cookies = strmap_get(headers, key);
    if (cookies == NULL) {
        key = "set-cookie";
        cookies = strmap_get(headers, key);
        if (cookies == NULL)
            return;
    }

    host_and_port = uri_host_and_port(request2->request->pool, uri);
    if (host_and_port == NULL)
        return;

    session = request_make_session(request2);
    if (session == NULL)
        return;

    do {
        cookie_jar_set_cookie2(session->cookies, cookies, host_and_port,
                               uri_path(uri));

        cookies = strmap_get_next(headers, key, cookies);
    } while (cookies != NULL);

    session_put(session);
}

static void
proxy_response(http_status_t status, struct strmap *headers,
               istream_t body, void *ctx)
{
    struct request *request2 = ctx;
    const struct translate_response *tr = request2->translate.response;

    assert(tr->address.type == RESOURCE_ADDRESS_HTTP);

    proxy_collect_cookies(request2, headers, tr->address.u.http->uri);

    http_response_handler_direct_response(&response_handler, request2,
                                          status, headers, body);
}

static void
proxy_abort(void *ctx)
{
    struct request *request2 = ctx;

    http_response_handler_direct_abort(&response_handler, request2);
}

static const struct http_response_handler proxy_response_handler = {
    .response = proxy_response,
    .abort = proxy_abort,
};

void
proxy_handler(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    struct forward_request forward;

    assert(tr->address.type == RESOURCE_ADDRESS_HTTP);

    request_forward(&forward, request2,
                    &tr->request_header_forward,
                    uri_host_and_port(request->pool, tr->address.u.http->uri),
                    uri_path(tr->address.u.http->uri));

#ifdef SPLICE
    if (forward.body != NULL)
        forward.body = istream_pipe_new(request->pool, forward.body,
                                        global_pipe_stock);
#endif

    http_cache_request(global_http_cache, request->pool,
                       forward.method, tr->address.u.http,
                       forward.headers, forward.body,
                       &proxy_response_handler, request2,
                       request2->async_ref);
}
