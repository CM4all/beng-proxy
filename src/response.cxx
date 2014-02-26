/*
 * Utilities for transforming the HTTP response being sent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.hxx"
#include "transformation.h"
#include "http_server.h"
#include "http_response.h"
#include "header-writer.h"
#include "header-forward.h"
#include "widget.h"
#include "widget-class.h"
#include "widget-dump.h"
#include "proxy_widget.hxx"
#include "session.h"
#include "fcache.h"
#include "http_address.h"
#include "strref-pool.h"
#include "growing-buffer.h"
#include "header-parser.h"
#include "global.h"
#include "resource-tag.h"
#include "hostname.h"
#include "dhashmap.h"
#include "errdoc.h"
#include "connection.h"
#include "bp_instance.hxx"
#include "strmap.h"
#include "pheaders.h"
#include "processor.h"
#include "css_processor.h"
#include "text_processor.h"
#include "istream.h"
#include "tvary.h"
#include "date.h"
#include "product.h"

#include <daemon/log.h>

static const char *
request_absolute_uri(const struct http_server_request *request,
                     const char *scheme, const char *host, const char *uri)
{
    assert(uri != nullptr);

    if (scheme == nullptr)
        scheme = "http";

    if (host == nullptr)
        host = strmap_get(request->headers, "host");

    if (host == nullptr || !hostname_is_well_formed(host))
        return nullptr;

    return p_strcat(request->pool,
                    scheme, "://",
                    host,
                    uri,
                    nullptr);
}

/**
 * Drop a widget and all its descendants from the session.
 *
 * @param session a locked session object
 * @param ref the top window to drop; nullptr drops all widgets
 */
static void
session_drop_widgets(struct session *session, const char *uri,
                     const struct widget_ref *ref)
{
    struct dhashmap *map = session->widgets;
    const char *id = uri;
    widget_session *ws;

    while (true) {
        if (map == nullptr)
            /* no such widget session (no children at all here) */
            return;

        ws = (widget_session *)dhashmap_get(map, id);
        if (ws == nullptr)
            /* no such widget session */
            return;

        if (ref == nullptr)
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

static void
response_invoke_processor(request &request2,
                          http_status_t status,
                          struct strmap *response_headers,
                          struct istream *body,
                          const struct transformation *transformation)
{
    struct http_server_request *request = request2.request;
    const char *uri;

    assert(!request2.response_sent);
    assert(body == nullptr || !istream_has_handler(body));

    if (body == nullptr) {
        response_dispatch_message(&request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Empty template cannot be processed");
        return;
    }

    if (!processable(response_headers)) {
        istream_close_unused(body);
        response_dispatch_message(&request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Invalid template content type");
        return;
    }

    struct widget *widget = NewFromPool<struct widget>(request->pool);
    widget_init_root(widget, request->pool,
                     request2.translate.response->uri != nullptr
                     ? request2.translate.response->uri
                     : strref_dup(request->pool, &request2.uri.base));

    const struct widget_ref *focus_ref =
        widget_ref_parse(request->pool,
                         strmap_remove_checked(request2.args, "focus"));

    const struct widget_ref *proxy_ref =
        widget_ref_parse(request->pool,
                         strmap_get_checked(request2.args, "frame"));

    if (focus_ref != nullptr && proxy_ref != nullptr &&
        !widget_ref_includes(proxy_ref, focus_ref)) {
        /* the focused widget is not reachable because it is not
           within the "frame" */

        focus_ref = nullptr;

        if (request->body != nullptr) {
            daemon_log(4, "discarding non-framed request body\n");
            istream_free_unused(&request2.body);
        }
    }

    widget->from_request.focus_ref = focus_ref;

    if (proxy_ref != nullptr)
        /* disable all following transformations, because we're doing
           a direct proxy request to a widget */
        request2.translate.transformation = nullptr;

    if (request2.translate.response->untrusted != nullptr &&
        proxy_ref == nullptr) {
        daemon_log(2, "refusing to render template on untrusted domain '%s'\n",
                   request2.translate.response->untrusted);
        istream_close_unused(body);
        response_dispatch_message(&request2, HTTP_STATUS_FORBIDDEN,
                                  "Forbidden");
        return;
    }

    if (request2.body != nullptr && widget->from_request.focus_ref != nullptr) {
        widget->for_focused.body = request2.body;
        request2.body = nullptr;
    }

    uri = request2.translate.response->uri != nullptr
        ? request2.translate.response->uri
        : request->uri;

    if (request2.translate.response->uri != nullptr)
        strref_set_c(&request2.uri.base, request2.translate.response->uri);

    /* make sure we have a session */
    struct session *session = request_make_session(&request2);
    if (session != nullptr) {
        if (widget->from_request.focus_ref == nullptr)
            /* drop the widget session and all descendants if there is
               no focus */
            session_drop_widgets(session, widget->id,
                                 proxy_ref);

        session_put(session);
    }

    http_method_t method = request->method;
    if (http_method_is_empty(method) &&
        request2.translate.transformation != nullptr)
        /* the following transformation may need the processed
           document to generate its headers, so we should not pass
           HEAD to the processor */
        method = HTTP_METHOD_GET;

    processor_env_init(request->pool, &request2.env,
                       request2.translate.response->site,
                       request2.translate.response->untrusted,
                       request->local_host_and_port, request->remote_host,
                       uri,
                       request_absolute_uri(request,
                                            request2.translate.response->scheme,
                                            request2.translate.response->host,
                                            uri),
                       &request2.uri,
                       request2.args,
                       request2.session_id,
                       method, request->headers);

    if (proxy_ref != nullptr) {
        /* the client requests a widget in proxy mode */

        proxy_widget(&request2, body,
                     widget, proxy_ref, transformation->u.processor.options);
    } else {
        /* the client requests the whole template */
        body = processor_process(request->pool, body,
                                 widget, &request2.env,
                                 transformation->u.processor.options);
        assert(body != nullptr);

        if (request2.connection->instance->config.dump_widget_tree)
            body = widget_dump_tree_after_istream(request->pool, body, widget);

        /*
#ifndef NO_DEFLATE
        if (http_client_accepts_encoding(request->headers, "deflate")) {
            header_write(response_headers, "content-encoding", "deflate");
            body = istream_deflate_new(request->pool, body);
        }
#endif
        */

        response_headers = processor_header_forward(request->pool,
                                                    response_headers);

        http_response_handler_direct_response(&response_handler, &request2,
                                              status, response_headers, body);
    }
}

static bool
css_processable(const struct strmap *headers)
{
    const char *content_type;

    content_type = strmap_get_checked(headers, "content-type");
    return content_type != nullptr &&
        strncmp(content_type, "text/css", 8) == 0;
}

static void
response_invoke_css_processor(request &request2,
                              http_status_t status,
                              struct strmap *response_headers,
                              struct istream *body,
                              const struct transformation *transformation)
{
    struct http_server_request *request = request2.request;

    assert(!request2.response_sent);
    assert(body == nullptr || !istream_has_handler(body));

    if (body == nullptr) {
        response_dispatch_message(&request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Empty template cannot be processed");
        return;
    }

    if (!css_processable(response_headers)) {
        istream_close_unused(body);
        response_dispatch_message(&request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Invalid template content type");
        return;
    }

    struct widget *widget = NewFromPool<struct widget>(request->pool);
    widget_init_root(widget, request->pool,
                     strref_dup(request->pool, &request2.uri.base));

    if (request2.translate.response->untrusted != nullptr) {
        daemon_log(2, "refusing to render template on untrusted domain '%s'\n",
                   request2.translate.response->untrusted);
        istream_close_unused(body);
        response_dispatch_message(&request2, HTTP_STATUS_FORBIDDEN,
                                  "Forbidden");
        return;
    }

    const char *uri = request2.translate.response->uri != nullptr
        ? request2.translate.response->uri
        : request->uri;

    if (request2.translate.response->uri != nullptr)
        strref_set_c(&request2.uri.base, request2.translate.response->uri);

    processor_env_init(request->pool, &request2.env,
                       request2.translate.response->site,
                       request2.translate.response->untrusted,
                       request->local_host_and_port, request->remote_host,
                       uri,
                       request_absolute_uri(request,
                                            request2.translate.response->scheme,
                                            request2.translate.response->host,
                                            uri),
                       &request2.uri,
                       request2.args,
                       request2.session_id,
                       HTTP_METHOD_GET, request->headers);

    body = css_processor(request->pool, body,
                         widget, &request2.env,
                         transformation->u.css_processor.options);
    assert(body != nullptr);

    response_headers = processor_header_forward(request->pool,
                                                response_headers);

    http_response_handler_direct_response(&response_handler, &request2,
                                          status, response_headers, body);
}

static void
response_invoke_text_processor(request &request2,
                               http_status_t status,
                               struct strmap *response_headers,
                               struct istream *body)
{
    struct http_server_request *request = request2.request;

    assert(!request2.response_sent);
    assert(body == nullptr || !istream_has_handler(body));

    if (body == nullptr) {
        response_dispatch_message(&request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Empty template cannot be processed");
        return;
    }

    if (!text_processor_allowed(response_headers)) {
        istream_close_unused(body);
        response_dispatch_message(&request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Invalid template content type");
        return;
    }

    struct widget *widget = NewFromPool<struct widget>(request->pool);
    widget_init_root(widget, request->pool,
                     strref_dup(request->pool, &request2.uri.base));

    if (request2.translate.response->untrusted != nullptr) {
        daemon_log(2, "refusing to render template on untrusted domain '%s'\n",
                   request2.translate.response->untrusted);
        istream_close_unused(body);
        response_dispatch_message(&request2, HTTP_STATUS_FORBIDDEN,
                                  "Forbidden");
        return;
    }

    const char *uri = request2.translate.response->uri != nullptr
        ? request2.translate.response->uri
        : request->uri;

    if (request2.translate.response->uri != nullptr)
        strref_set_c(&request2.uri.base, request2.translate.response->uri);

    processor_env_init(request->pool, &request2.env,
                       request2.translate.response->site,
                       request2.translate.response->untrusted,
                       request->local_host_and_port, request->remote_host,
                       uri,
                       request_absolute_uri(request,
                                            request2.translate.response->scheme,
                                            request2.translate.response->host,
                                            uri),
                       &request2.uri,
                       request2.args,
                       request2.session_id,
                       HTTP_METHOD_GET, request->headers);

    body = text_processor(request->pool, body,
                          widget, &request2.env);
    assert(body != nullptr);

    response_headers = processor_header_forward(request->pool,
                                                response_headers);

    http_response_handler_direct_response(&response_handler, &request2,
                                          status, response_headers, body);
}

/**
 * Append response headers set by the translation server.
 */
static void
translation_response_headers(struct growing_buffer *headers,
                             const struct translate_response *tr)
{
    if (tr->www_authenticate != nullptr)
        header_write(headers, "www-authenticate", tr->www_authenticate);

    if (tr->authentication_info != nullptr)
        header_write(headers, "authentication-info", tr->authentication_info);

    if (tr->headers != nullptr) {
        strmap_rewind(tr->headers);

        const struct strmap_pair *pair;
        while ((pair = strmap_next(tr->headers)) != nullptr)
            header_write(headers, pair->key, pair->value);
    }
}

/**
 * Generate additional response headers as needed.
 */
static struct growing_buffer *
more_response_headers(const request &request2,
                      struct growing_buffer *headers)
{
    if (headers == nullptr)
        headers = growing_buffer_new(request2.request->pool, 256);

    /* RFC 2616 3.8: Product Tokens */
    header_write(headers, "server", request2.product_token != nullptr
                 ? request2.product_token
                 : BRIEF_PRODUCT_TOKEN);

#ifndef NO_DATE_HEADER
    /* RFC 2616 14.18: Date */
    header_write(headers, "date", request2.date != nullptr
                 ? request2.date
                 : http_date_format(time(nullptr)));
#endif

    const struct translate_response *tr = request2.translate.response;
    translation_response_headers(headers, tr);

    return headers;
}

/**
 * Generate the Set-Cookie response header for the given request.
 */
static void
response_generate_set_cookie(request &request2,
                             struct growing_buffer *headers)
{
    assert(!request2.stateless);
    assert(request2.session_cookie != nullptr);
    assert(headers != nullptr);

    if (request2.send_session_cookie) {
        header_write_begin(headers, "set-cookie");
        growing_buffer_write_string(headers, request2.session_cookie);
        growing_buffer_write_buffer(headers, "=", 1);
        growing_buffer_write_string(headers,
                                    session_id_format(request2.session_id,
                                                      &request2.session_id_string));
        growing_buffer_write_string(headers,
                                    "; HttpOnly; Path=/; Version=1");

        if (request2.translate.response->secure_cookie)
            growing_buffer_write_string(headers, "; Secure");

        if (request2.translate.response->cookie_domain != nullptr) {
            growing_buffer_write_string(headers, "; Domain=\"");
            growing_buffer_write_string(headers,
                                        request2.translate.response->cookie_domain);
            growing_buffer_write_string(headers, "\"");
        }

        /* "Discard" must be last, to work around an Android bug*/
        growing_buffer_write_string(headers, "; Discard");

        header_write_finish(headers);

        /* workaround for IE10 bug; see
           http://projects.intern.cm-ag/view.php?id=3789 for
           details */
        header_write(headers, "p3p", "CP=\"CAO PSA OUR\"");

        struct session *session = request_make_session(&request2);
        if (session != nullptr) {
            session->cookie_sent = true;
            session_put(session);
        }
    } else if (request2.translate.response->discard_session &&
               !session_id_is_defined(request2.session_id)) {
        /* delete the cookie for the discarded session */
        header_write_begin(headers, "set-cookie");
        growing_buffer_write_string(headers, request2.session_cookie);
        growing_buffer_write_string(headers,
                                    "=; HttpOnly; Path=/; Version=1"
                                    "; Max-Age=0");

        if (request2.translate.response->cookie_domain != nullptr) {
            growing_buffer_write_string(headers, "; Domain=\"");
            growing_buffer_write_string(headers,
                                        request2.translate.response->cookie_domain);
            growing_buffer_write_string(headers, "\"");
        }

        /* "Discard" must be last, to work around an Android bug*/
        growing_buffer_write_string(headers, "; Discard");

        header_write_finish(headers);
    }
}

/*
 * dispatch
 *
 */

static void
response_dispatch_direct(request &request2,
                         http_status_t status, struct growing_buffer *headers,
                         struct istream *body)
{
    assert(!request2.response_sent);
    assert(body == nullptr || !istream_has_handler(body));

    if (http_status_is_success(status) &&
        request2.translate.response->www_authenticate != nullptr)
        /* default to "401 Unauthorized" */
        status = HTTP_STATUS_UNAUTHORIZED;

    headers = more_response_headers(request2, headers);

    request_discard_body(&request2);

    if (!request2.stateless)
        response_generate_set_cookie(request2, headers);

#ifdef SPLICE
    if (body != nullptr)
        body = istream_pipe_new(request2.request->pool, body,
                                global_pipe_stock);
#endif

#ifndef NDEBUG
    request2.response_sent = true;
#endif

    http_server_response(request2.request, status, headers, body);
}

static void
response_apply_filter(request &request2,
                      http_status_t status, struct strmap *headers2,
                      struct istream *body,
                      const struct resource_address *filter)
{
    struct http_server_request *request = request2.request;
    const char *source_tag;

    source_tag = resource_tag_append_etag(request->pool,
                                          request2.resource_tag, headers2);
    request2.resource_tag = source_tag != nullptr
        ? p_strcat(request->pool, source_tag, "|",
                   resource_address_id(filter, request->pool),
                   nullptr)
        : nullptr;

#ifdef SPLICE
    if (body != nullptr)
        body = istream_pipe_new(request->pool, body, global_pipe_stock);
#endif

    filter_cache_request(global_filter_cache, request->pool, filter,
                         source_tag, status, headers2, body,
                         &response_handler, &request2,
                         &request2.async_ref);
}

static void
response_apply_transformation(request &request2,
                              http_status_t status, struct strmap *headers,
                              struct istream *body,
                              const struct transformation *transformation)
{
    assert(transformation != nullptr);

    request2.transformed = true;

    switch (transformation->type) {
    case transformation::TRANSFORMATION_FILTER:
        response_apply_filter(request2, status, headers, body,
                              &transformation->u.filter);
        break;

    case transformation::TRANSFORMATION_PROCESS:
        /* processor responses cannot be cached */
        request2.resource_tag = nullptr;

        response_invoke_processor(request2, status, headers, body,
                                  transformation);
        break;

    case transformation::TRANSFORMATION_PROCESS_CSS:
        /* processor responses cannot be cached */
        request2.resource_tag = nullptr;

        response_invoke_css_processor(request2, status, headers, body,
                                      transformation);

    case transformation::TRANSFORMATION_PROCESS_TEXT:
        /* processor responses cannot be cached */
        request2.resource_tag = nullptr;

        response_invoke_text_processor(request2, status, headers, body);
    }
}

static bool
filter_enabled(const struct translate_response *tr,
               http_status_t status)
{
    return http_status_is_success(status) ||
        (http_status_is_client_error(status) && tr->filter_4xx);
}

void
response_dispatch(struct request *request2,
                  http_status_t status, struct growing_buffer *headers,
                  struct istream *body)
{
    assert(!request2->response_sent);
    assert(body == nullptr || !istream_has_handler(body));

    if (http_status_is_error(status) && !request2->transformed &&
        request2->translate.response->error_document) {
        request2->transformed = true;

        /* for sure, the errdoc library doesn't use the request body;
           discard it as early as possible */
        request_discard_body(request2);

        errdoc_dispatch_response(request2, status, headers, body);
        return;
    }

    /* if HTTP status code is not successful: don't apply
       transformation on the error document */
    const struct transformation *transformation
        = request2->translate.transformation;
    if (transformation != nullptr &&
        filter_enabled(request2->translate.response, status)) {
        struct strmap *headers2;

        request2->translate.transformation = transformation->next;

        if (headers != nullptr) {
            struct http_server_request *request = request2->request;
            headers2 = strmap_new(request->pool, 41);
            header_parse_buffer(request->pool, headers2, headers);
        } else
            headers2 = nullptr;

        response_apply_transformation(*request2, status, headers2, body,
                                      transformation);
    } else
        response_dispatch_direct(*request2, status, headers, body);
}

void
response_dispatch_message2(struct request *request2, http_status_t status,
                           struct growing_buffer *headers, const char *msg)
{
    struct pool *pool = request2->request->pool;

    assert(request2 != nullptr);
    assert(http_status_is_valid(status));
    assert(msg != nullptr);

    if (headers == nullptr)
        headers = growing_buffer_new(pool, 256);

    header_write(headers, "content-type", "text/plain");

    response_dispatch(request2, status, headers,
                      istream_string_new(pool, msg));
}

void
response_dispatch_message(struct request *request2, http_status_t status,
                          const char *msg)
{
    response_dispatch_message2(request2, status, nullptr, msg);
}

void
response_dispatch_redirect(struct request *request2, http_status_t status,
                           const char *location, const char *msg)
{
    struct pool *pool = request2->request->pool;

    assert(status >= 300 && status < 400);
    assert(location != nullptr);

    if (msg == nullptr)
        msg = "redirection";

    struct growing_buffer *headers = growing_buffer_new(pool, 256);
    header_write(headers, "location", location);

    response_dispatch_message2(request2, status, headers, msg);
}

/*
 * HTTP response handler
 *
 */

static void
response_response(http_status_t status, struct strmap *headers,
                  struct istream *body,
                  void *ctx)
{
    request &request2 = *(request *)ctx;
    struct http_server_request *request = request2.request;
    struct growing_buffer *response_headers;

    assert(!request2.response_sent);
    assert(body == nullptr || !istream_has_handler(body));

    if (request2.translate.transformation != nullptr &&
        http_status_is_success(status)) {
        const struct transformation *transformation
            = request2.translate.transformation;
        request2.translate.transformation = transformation->next;

        response_apply_transformation(request2, status, headers, body,
                                      transformation);
        return;
    }

    const struct strmap *original_headers = headers;

    headers = forward_response_headers(request->pool, headers,
                                       request->local_host_and_port,
                                       &request2.translate.response->response_header_forward);

    headers = add_translation_vary_header(request->pool, headers,
                                          request2.translate.response);

    request2.product_token = strmap_remove(headers, "server");

#ifdef NO_DATE_HEADER
    request2.date = strmap_remove(headers, "date");
#endif

    response_headers = headers != nullptr
        ? headers_dup(request->pool, headers)
        : nullptr;
    if (original_headers != nullptr && request->method == HTTP_METHOD_HEAD)
        /* pass Content-Length, even though there is no response body
           (RFC 2616 14.13) */
        headers_copy_one(original_headers, response_headers, "content-length");

    response_dispatch(&request2,
                      status, response_headers,
                      body);
}

static void
response_abort(GError *error, void *ctx)
{
    request &request2 = *(request *)ctx;

    assert(!request2.response_sent);

    daemon_log(2, "error on %s: %s\n", request2.request->uri, error->message);

    response_dispatch_error(&request2, error);

    g_error_free(error);
}

const struct http_response_handler response_handler = {
    .response = response_response,
    .abort = response_abort,
};
