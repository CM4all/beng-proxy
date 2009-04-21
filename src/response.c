/*
 * Utilities for transforming the HTTP response being sent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.h"
#include "transformation.h"
#include "http-server.h"
#include "header-writer.h"
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

#include <daemon/log.h>

static const char *const copy_headers[] = {
    "age",
    "etag",
    "cache-control",
    "content-encoding",
    "content-language",
    "content-md5",
    "content-range",
    "content-type",
    "last-modified",
    "retry-after",
    "vary",
    NULL,
};

static const char *const copy_headers_processed[] = {
    "cache-control",
    "content-language",
    "content-type",
    "vary",
    NULL,
};


static const char *
request_absolute_uri(const struct http_server_request *request)
{
    const char *host = strmap_get(request->headers, "host");

    if (host == NULL)
        return NULL;

    return p_strcat(request->pool,
                    "http://",
                    host,
                    request->uri,
                    NULL);
}


/*
 * processor invocation
 *
 */

static bool
processable(struct strmap *headers)
{
    const char *content_type;

    if (headers == NULL)
        return false;

    content_type = strmap_get(headers, "content-type");
    return content_type != NULL &&
        (strncmp(content_type, "text/html", 9) == 0 ||
         strncmp(content_type, "text/xml", 8) == 0);
}

static void
response_invoke_processor(struct request *request2,
                          http_status_t status, struct growing_buffer *response_headers,
                          istream_t body,
                          const struct transformation *transformation)
{
    struct http_server_request *request = request2->request;
    struct strmap *headers;
    istream_t request_body;
    struct widget *widget;

    assert(!request2->response_sent);
    assert(body == NULL || !istream_has_handler(body));

    if (body == NULL) {
        response_dispatch(request2, status, response_headers, NULL);
        return;
    }

    if (response_headers != NULL) {
        headers = strmap_new(request->pool, 16);
        header_parse_buffer(request->pool, headers, response_headers);
    } else
        headers = NULL;

    if (!processable(headers)) {
        http_server_send_message(request, HTTP_STATUS_BAD_GATEWAY,
                                 "Invalid template content type");
        return;
    }

    if (http_server_request_has_body(request) && !request2->body_consumed) {
        request_body = request->body;
        request2->body_consumed = true;
    } else {
        request_body = NULL;
    }

    request_make_session(request2);

    processor_env_init(request->pool, &request2->env,
                       transformation->u.processor.domain,
                       request->remote_host,
                       request_absolute_uri(request),
                       &request2->uri,
                       request2->args,
                       request2->session_id,
                       request->headers,
                       request_body);

    widget = p_malloc(request->pool, sizeof(*widget));
    widget_init(widget, &root_widget_class);
    widget->id = strref_dup(request->pool, &request2->uri.base);
    widget->lazy.path = "";
    widget->lazy.prefix = "__";

    widget->from_request.focus_ref = widget_ref_parse(request->pool,
                                                      strmap_remove(request2->env.args, "focus"));

    widget->from_request.proxy_ref = widget_ref_parse(request->pool,
                                                      strmap_get(request2->env.args, "frame"));

#ifdef DUMP_WIDGET_TREE
    request2->dump_widget_tree = widget;
#endif

    if (widget->from_request.proxy_ref != NULL) {
        /* the client requests a widget in proxy mode */

        processor_new(request->pool, headers, body,
                      widget, &request2->env,
                      transformation->u.processor.options,
                      &widget_proxy_handler, request,
                      request2->async_ref);
    } else {
        /* the client requests the whole template */
        processor_new(request->pool, headers, body, widget, &request2->env,
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


/*
 * dispatch
 *
 */

void
response_dispatch(struct request *request2,
                  http_status_t status, struct growing_buffer *headers,
                  istream_t body)
{
    const struct transformation *transformation
        = request2->translate.transformation;

    assert(!request2->response_sent);
    assert(body == NULL || !istream_has_handler(body));

    if (transformation)
        request2->translate.transformation = transformation->next;

    if (transformation != NULL &&
        transformation->type == TRANSFORMATION_FILTER) {
        struct http_server_request *request = request2->request;
        struct strmap *headers2;

        if (headers != NULL) {
            headers2 = strmap_new(request->pool, 16);
            header_parse_buffer(request->pool, headers2, headers);
        } else
            headers2 = NULL;

        filter_cache_request(global_filter_cache, request->pool,
                             &transformation->u.filter,
                             headers2, body,
                             &response_handler, request2,
                             request2->async_ref);
    } else if (transformation != NULL &&
               transformation->type == TRANSFORMATION_PROCESS) {
        response_invoke_processor(request2, status, headers, body,
                                  transformation);
    } else {
        header_write(headers, "server", "beng-proxy v" VERSION);

        request2->response_sent = true;
        http_server_response(request2->request,
                             status, headers, body);
    }
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

    if (headers == NULL) {
        response_headers = growing_buffer_new(request->pool, 1024);
    } else {
        response_headers = growing_buffer_new(request->pool, 2048);
        if (request2->translate.transformation != NULL &&
            request2->translate.transformation->type == TRANSFORMATION_PROCESS)
            headers_copy(headers, response_headers, copy_headers_processed);
        else
            headers_copy(headers, response_headers, copy_headers);
    }

    response_dispatch(request2,
                      status, response_headers,
                      body);
}

static void
response_abort(void *ctx)
{
    struct request *request = ctx;

    if (!request->response_sent)
        http_server_send_message(request->request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
}

const struct http_response_handler response_handler = {
    .response = response_response,
    .abort = response_abort,
};
