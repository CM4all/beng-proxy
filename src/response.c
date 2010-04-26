/*
 * Utilities for transforming the HTTP response being sent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.h"
#include "transformation.h"
#include "http-server.h"
#include "header-writer.h"
#include "header-forward.h"
#include "widget.h"
#include "embed.h"
#include "proxy-widget.h"
#include "session.h"
#include "fcache.h"
#include "uri-address.h"
#include "strref-pool.h"
#include "growing-buffer.h"
#include "header-parser.h"
#include "global.h"
#include "resource-tag.h"
#include "hostname.h"
#include "dhashmap.h"

#include <daemon/log.h>

static const char *
request_absolute_uri(const struct http_server_request *request,
                     const char *scheme, const char *host, const char *uri)
{
    assert(uri != NULL);

    if (scheme == NULL)
        scheme = "http";

    if (host == NULL)
        host = strmap_get(request->headers, "host");

    if (host == NULL || !hostname_is_well_formed(host))
        return NULL;

    return p_strcat(request->pool,
                    scheme, "://",
                    host,
                    uri,
                    NULL);
}

/**
 * Drop a widget and all its descendants from the session.
 *
 * @param session a locked session object
 * @param ref the top window to drop; NULL drops all widgets
 */
static void
session_drop_widgets(struct session *session, const char *uri,
                     const struct widget_ref *ref)
{
    struct dhashmap *map = session->widgets;
    const char *id = uri;
    struct widget_session *ws;

    while (true) {
        if (map == NULL)
            /* no such widget session (no children at all here) */
            return;

        ws = dhashmap_get(map, id);
        if (ws == NULL)
            /* no such widget session */
            return;

        if (ref == NULL)
            /* found the widget session */
            break;

        map = ws->children;
        id = ref->id;
        ref = ref->next;
    }

    dhashmap_remove(map, id);
    widget_session_delete(session->pool, ws);
}


/*
 * processor invocation
 *
 */

static bool
processable(const struct strmap *headers)
{
    const char *content_type;

    content_type = strmap_get_checked(headers, "content-type");
    return content_type != NULL &&
        (strncmp(content_type, "text/html", 9) == 0 ||
         strncmp(content_type, "text/xml", 8) == 0 ||
         strncmp(content_type, "application/xhtml+xml", 21) == 0);
}

static void
response_invoke_processor(struct request *request2,
                          http_status_t status,
                          struct strmap *response_headers,
                          istream_t body,
                          const struct transformation *transformation)
{
    struct http_server_request *request = request2->request;
    istream_t request_body;
    struct widget *widget;
    const char *uri;

    assert(!request2->response_sent);
    assert(body == NULL || !istream_has_handler(body));

    if (body == NULL) {
        request_discard_body(request2);
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Empty template cannot be processed");
        return;
    }

    if (!processable(response_headers)) {
        istream_close(body);
        request_discard_body(request2);
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Invalid template content type");
        return;
    }

    widget = p_malloc(request->pool, sizeof(*widget));
    widget_init(widget, request->pool, &root_widget_class);
    widget->id = strref_dup(request->pool, &request2->uri.base);
    widget->lazy.path = "";
    widget->lazy.prefix = "__";

    widget->from_request.focus_ref =
        widget_ref_parse(request->pool,
                         strmap_remove_checked(request2->args, "focus"));

    widget->from_request.proxy_ref =
        widget_ref_parse(request->pool,
                         strmap_get_checked(request2->args, "frame"));

    if (request2->translate.response->untrusted != NULL &&
        widget->from_request.proxy_ref == NULL) {
        daemon_log(2, "refusing to render template on untrusted domain '%s'\n",
                   request2->translate.response->untrusted);
        istream_close(body);
        request_discard_body(request2);
        response_dispatch_message(request2, HTTP_STATUS_FORBIDDEN,
                                  "Forbidden");
        return;
    }

    if (http_server_request_has_body(request) && !request2->body_consumed &&
        widget->from_request.focus_ref != NULL) {
        request_body = request->body;
        request2->body_consumed = true;
    } else {
        request_body = NULL;
    }

    uri = request2->translate.response->uri != NULL
        ? request2->translate.response->uri
        : request->uri;

    if (request2->translate.response->uri != NULL)
        strref_set_c(&request2->uri.base, request2->translate.response->uri);

    /* make sure we have a session */
    struct session *session = request_make_session(request2);
    if (session != NULL) {
        if (widget->from_request.focus_ref == NULL)
            /* drop the widget session and all descendants if there is
               no focus */
            session_drop_widgets(session,
                                 strref_dup(request->pool, &request2->uri.base),
                                 widget->from_request.proxy_ref);

        session_put(session);
    }

    processor_env_init(request->pool, &request2->env,
                       request2->translate.response->untrusted,
                       request->local_host, request->remote_host,
                       uri,
                       request_absolute_uri(request,
                                            request2->translate.response->scheme,
                                            request2->translate.response->host,
                                            uri),
                       &request2->uri,
                       request2->args,
                       request2->session_id,
                       request->method, request->headers,
                       request_body);

#ifdef DUMP_WIDGET_TREE
    request2->dump_widget_tree = widget;
#endif

    if (widget->from_request.proxy_ref != NULL) {
        /* the client requests a widget in proxy mode */

        processor_new(request->pool, status, response_headers, body,
                      widget, &request2->env,
                      transformation->u.processor.options,
                      &widget_proxy_handler, request2,
                      request2->async_ref);
    } else {
        /* the client requests the whole template */
        processor_new(request->pool, status, response_headers, body,
                      widget, &request2->env,
                      transformation->u.processor.options,
                      &response_handler, request2,
                      request2->async_ref);
    }

    /*
#ifndef NO_DEFLATE
    if (http_client_accepts_encoding(request->headers, "deflate")) {
        header_write(response_headers, "content-encoding", "deflate");
        body = istream_deflate_new(request->pool, body);
    }
#endif
    */
}


/**
 * Generate additional response headers as needed.
 */
static struct growing_buffer *
more_response_headers(const struct request *request2,
                      struct growing_buffer *headers)
{
    if (headers == NULL)
        headers = growing_buffer_new(request2->request->pool, 256);

    /* RFC 2616 3.8: Product Tokens */
    header_write(headers, "server", request2->product_token != NULL
                 ? request2->product_token
                 : "beng-proxy/" VERSION);

    return headers;
}

/*
 * dispatch
 *
 */

static void
response_dispatch_direct(struct request *request2,
                         http_status_t status, struct growing_buffer *headers,
                         istream_t body)
{
    assert(!request2->response_sent);
    assert(body == NULL || !istream_has_handler(body));

    headers = more_response_headers(request2, headers);

    request_discard_body(request2);

    if (request2->send_session_cookie) {
        struct session *session;

        header_write(headers, "set-cookie",
                     p_strcat(request2->request->pool,
                              "beng_proxy_session=",
                              session_id_format(request2->session_id,
                                                &request2->session_id_string),
                              "; Discard; HttpOnly; Path=/; Version=1",
                              NULL));

        session = request_make_session(request2);
        if (session != NULL) {
            session->cookie_sent = true;
            session_put(session);
        }
    } else if (request2->translate.response->discard_session &&
               !session_id_is_defined(request2->session_id)) {
        /* delete the cookie for the discarded session */
        header_write(headers, "set-cookie",
                     "beng_proxy_session=; Discard; HttpOnly; "
                     "Path=/; Max-Age=0; Version=1");
    }

#ifdef SPLICE
    if (body != NULL)
        body = istream_pipe_new(request2->request->pool, body,
                                global_pipe_stock);
#endif

#ifndef NDEBUG
    request2->response_sent = true;
#endif

    http_server_response(request2->request, status, headers, body);
}

static void
response_apply_filter(struct request *request2,
                      http_status_t status, struct strmap *headers2,
                      istream_t body,
                      const struct resource_address *filter)
{
    struct http_server_request *request = request2->request;
    const char *source_tag;

    source_tag = resource_tag_append_etag(request->pool,
                                          request2->resource_tag, headers2);
    request2->resource_tag = source_tag != NULL
        ? p_strcat(request->pool, source_tag, "|",
                   resource_address_id(filter, request->pool),
                   NULL)
        : NULL;

    filter_cache_request(global_filter_cache, request->pool, filter,
                         source_tag, status, headers2, body,
                         &response_handler, request2,
                         request2->async_ref);
}

static void
response_apply_transformation(struct request *request2,
                              http_status_t status, struct strmap *headers,
                              istream_t body,
                              const struct transformation *transformation)
{
    assert(transformation != NULL);

    switch (transformation->type) {
    case TRANSFORMATION_FILTER:
        response_apply_filter(request2, status, headers, body,
                              &transformation->u.filter);
        break;

    case TRANSFORMATION_PROCESS:
        /* processor responses cannot be cached */
        request2->resource_tag = NULL;

        response_invoke_processor(request2, status, headers, body,
                                  transformation);
        break;
    }
}

void
response_dispatch(struct request *request2,
                  http_status_t status, struct growing_buffer *headers,
                  istream_t body)
{
    const struct transformation *transformation
        = request2->translate.transformation;

    assert(!request2->response_sent);
    assert(body == NULL || !istream_has_handler(body));

    /* if HTTP status code is not successful: don't apply
       transformation on the error document */
    if (transformation != NULL && http_status_is_success(status)) {
        struct strmap *headers2;

        request2->translate.transformation = transformation->next;

        if (headers != NULL) {
            struct http_server_request *request = request2->request;
            headers2 = strmap_new(request->pool, 41);
            header_parse_buffer(request->pool, headers2, headers);
        } else
            headers2 = NULL;

        response_apply_transformation(request2, status, headers2, body,
                                      transformation);
    } else
        response_dispatch_direct(request2, status, headers, body);
}

void
response_dispatch_message(struct request *request2, http_status_t status,
                          const char *msg)
{
    pool_t pool = request2->request->pool;
    struct growing_buffer *headers = growing_buffer_new(pool, 256);
    header_write(headers, "content-type", "text/plain");

    response_dispatch(request2, status, headers,
                      istream_string_new(pool, msg));
}

void
response_dispatch_redirect(struct request *request2, http_status_t status,
                           const char *location, const char *msg)
{
    pool_t pool = request2->request->pool;

    assert(status >= 300 && status < 400);
    assert(location != NULL);

    if (msg == NULL)
        msg = "redirection";

    struct growing_buffer *headers = growing_buffer_new(pool, 256);
    header_write(headers, "location", location);

    response_dispatch(request2, status, headers,
                      istream_string_new(pool, msg));
}


/*
 * debug
 *
 */

#ifdef DUMP_WIDGET_TREE
static void dump_widget_tree(unsigned indent, const struct widget *widget)
{
    const struct widget *child;

    daemon_log(4, "%*swidget id='%s' class='%s'\n", indent, "",
               widget->id, widget->class_name);

    for (child = (const struct widget *)widget->children.next;
         &child->siblings != &widget->children;
         child = (const struct widget *)child->siblings.next)
        dump_widget_tree(indent + 2, widget);
}
#endif

/*
 * HTTP response handler
 *
 */

static void
response_response(http_status_t status, struct strmap *headers,
                  istream_t body,
                  void *ctx)
{
    struct request *request2 = ctx;
    struct http_server_request *request = request2->request;
    struct growing_buffer *response_headers;

    assert(!request2->response_sent);
    assert(body == NULL || !istream_has_handler(body));

#ifdef DUMP_WIDGET_TREE
    if (request2->dump_widget_tree != NULL) {
        if (body == NULL) {
            daemon_log(4, "dumping widget tree of request '%s'\n", request->uri);
            dump_widget_tree(0, request2->dump_widget_tree);
            request2->dump_widget_tree = NULL;
        } /* else
             XXX find some way to print widget tree after stream's EOF
          */
    }
#endif

    if (request2->translate.transformation != NULL &&
        http_status_is_success(status)) {
        const struct transformation *transformation
            = request2->translate.transformation;
        request2->translate.transformation = transformation->next;

        response_apply_transformation(request2, status, headers, body,
                                      transformation);
        return;
    }

    headers = forward_response_headers(request->pool, headers,
                                       request->local_host,
                                       &request2->translate.response->response_header_forward);

    request2->product_token = strmap_remove(headers, "server");

    response_headers = headers != NULL
        ? headers_dup(request->pool, headers)
        : NULL;

    response_dispatch(request2,
                      status, response_headers,
                      body);
}

static void
response_abort(void *ctx)
{
    struct request *request = ctx;

    assert(!request->response_sent);

    request_discard_body(request);
    response_dispatch_message(request,
                              HTTP_STATUS_INTERNAL_SERVER_ERROR,
                              "Internal server error");
}

const struct http_response_handler response_handler = {
    .response = response_response,
    .abort = response_abort,
};
