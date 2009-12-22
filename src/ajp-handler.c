/*
 * Serve HTTP requests from an AJPv13 server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "request.h"
#include "request-forward.h"
#include "http-server.h"
#include "ajp-request.h"
#include "uri-address.h"
#include "global.h"
#include "cookie-client.h"
#include "uri-extract.h"

static const char *
extract_server_name(const struct strmap *headers)
{
    const char *p = strmap_get_checked(headers, "host");
    if (p == NULL)
        return ""; /* XXX */

    /* XXX remove port? */
    return p;
}

static void
ajp_handler_collect_cookies(struct request *request2, const struct strmap *headers,
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
        cookie_jar_set_cookie2(session->cookies, cookies, host_and_port);

        cookies = strmap_get_next(headers, key, cookies);
    } while (cookies != NULL);

    session_put(session);
}

static void
ajp_handler_response(http_status_t status, struct strmap *headers,
                     istream_t body, void *ctx)
{
    struct request *request2 = ctx;
    const struct translate_response *tr = request2->translate.response;

    assert(tr->address.type == RESOURCE_ADDRESS_AJP);

    ajp_handler_collect_cookies(request2, headers, tr->address.u.http->uri);

    http_response_handler_direct_response(&response_handler, request2,
                                          status, headers, body);
}

static void
ajp_handler_abort(void *ctx)
{
    struct request *request2 = ctx;

    http_response_handler_direct_abort(&response_handler, request2);
}

static const struct http_response_handler ajp_response_handler = {
    .response = ajp_handler_response,
    .abort = ajp_handler_abort,
};

void
ajp_handler(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    struct forward_request forward;

    assert(tr->address.type == RESOURCE_ADDRESS_AJP);

    request_forward(&forward, request2,
                    &tr->request_header_forward,
                    uri_host_and_port(request->pool, tr->address.u.http->uri),
                    tr->address.u.http->uri);

    /* do it */

    ajp_stock_request(request->pool, global_tcp_stock,
                      "http", request->remote_host, request->remote_host,
                      extract_server_name(request->headers),
                      80, /* XXX */
                      false,
                      forward.method, tr->address.u.http,
                      forward.headers, forward.body,
                      &ajp_response_handler, request2,
                      request2->async_ref);
}
