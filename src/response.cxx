/*
 * Utilities for transforming the HTTP response being sent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.hxx"
#include "transformation.hxx"
#include "http_server.hxx"
#include "http_headers.hxx"
#include "http_response.hxx"
#include "header_writer.hxx"
#include "header_parser.hxx"
#include "header_forward.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "widget_dump.hxx"
#include "proxy_widget.hxx"
#include "session.hxx"
#include "fcache.hxx"
#include "strref_pool.hxx"
#include "growing_buffer.hxx"
#include "bp_global.hxx"
#include "resource_tag.hxx"
#include "hostname.hxx"
#include "errdoc.hxx"
#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "strmap.hxx"
#include "pheaders.hxx"
#include "processor.h"
#include "css_processor.h"
#include "text_processor.hxx"
#include "istream.h"
#include "istream_pipe.hxx"
#include "istream_string.hxx"
#include "tvary.hxx"
#include "date.h"
#include "product.h"

#include <daemon/log.h>

static const char *
request_absolute_uri(const struct http_server_request &request,
                     const char *scheme, const char *host, const char *uri)
{
    assert(uri != nullptr);

    if (scheme == nullptr)
        scheme = "http";

    if (host == nullptr)
        host = request.headers->Get("host");

    if (host == nullptr || !hostname_is_well_formed(host))
        return nullptr;

    return p_strcat(request.pool,
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
session_drop_widgets(Session &session, const char *uri,
                     const struct widget_ref *ref)
{
    WidgetSession::Set *map = &session.widgets;
    const char *id = uri;

    while (true) {
        auto i = map->find(id, WidgetSession::Compare());
        if (i == map->end())
            /* no such widget session */
            return;

        auto &ws = *i;

        if (ref == nullptr) {
            /* found the widget session */
            map->erase(i);
            widget_session_delete(session.pool, &ws);
        }

        map = &ws.children;
        id = ref->id;
        ref = ref->next;
    }
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
                          const Transformation &transformation)
{
    struct http_server_request *request = request2.request;
    const char *uri;

    assert(!request2.response_sent);
    assert(body == nullptr || !istream_has_handler(body));

    if (body == nullptr) {
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Empty template cannot be processed");
        return;
    }

    if (!processable(response_headers)) {
        istream_close_unused(body);
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Invalid template content type");
        return;
    }

    struct widget *widget = NewFromPool<struct widget>(*request->pool);
    widget->InitRoot(*request->pool,
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
        request2.CancelTransformations();

    if (request2.translate.response->untrusted != nullptr &&
        proxy_ref == nullptr) {
        daemon_log(2, "refusing to render template on untrusted domain '%s'\n",
                   request2.translate.response->untrusted);
        istream_close_unused(body);
        response_dispatch_message(request2, HTTP_STATUS_FORBIDDEN,
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
    auto *session = request_make_session(request2);
    if (session != nullptr) {
        if (widget->from_request.focus_ref == nullptr)
            /* drop the widget session and all descendants if there is
               no focus */
            session_drop_widgets(*session, widget->id,
                                 proxy_ref);

        session_put(session);
    }

    http_method_t method = request->method;
    if (http_method_is_empty(method) && request2.HasTransformations())
        /* the following transformation may need the processed
           document to generate its headers, so we should not pass
           HEAD to the processor */
        method = HTTP_METHOD_GET;

    request2.env = processor_env(request->pool,
                                 request2.translate.response->site,
                                 request2.translate.response->untrusted,
                                 request->local_host_and_port, request->remote_host,
                                 uri,
                                 request_absolute_uri(*request,
                                                      request2.translate.response->scheme,
                                                      request2.translate.response->host,
                                                      uri),
                                 &request2.uri,
                                 request2.args,
                                 request2.session_cookie,
                                 request2.session_id,
                                 method, request->headers);

    if (proxy_ref != nullptr) {
        /* the client requests a widget in proxy mode */

        proxy_widget(request2, body,
                     widget, proxy_ref, transformation.u.processor.options);
    } else {
        /* the client requests the whole template */
        body = processor_process(request->pool, body,
                                 widget, &request2.env,
                                 transformation.u.processor.options);
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

        response_handler.InvokeResponse(&request2, status,
                                        response_headers, body);
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
                              const Transformation &transformation)
{
    struct http_server_request *request = request2.request;

    assert(!request2.response_sent);
    assert(body == nullptr || !istream_has_handler(body));

    if (body == nullptr) {
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Empty template cannot be processed");
        return;
    }

    if (!css_processable(response_headers)) {
        istream_close_unused(body);
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Invalid template content type");
        return;
    }

    struct widget *widget = NewFromPool<struct widget>(*request->pool);
    widget->InitRoot(*request->pool,
                     strref_dup(request->pool, &request2.uri.base));

    if (request2.translate.response->untrusted != nullptr) {
        daemon_log(2, "refusing to render template on untrusted domain '%s'\n",
                   request2.translate.response->untrusted);
        istream_close_unused(body);
        response_dispatch_message(request2, HTTP_STATUS_FORBIDDEN,
                                  "Forbidden");
        return;
    }

    const char *uri = request2.translate.response->uri != nullptr
        ? request2.translate.response->uri
        : request->uri;

    if (request2.translate.response->uri != nullptr)
        strref_set_c(&request2.uri.base, request2.translate.response->uri);

    request2.env = processor_env(request->pool,
                                 request2.translate.response->site,
                                 request2.translate.response->untrusted,
                                 request->local_host_and_port, request->remote_host,
                                 uri,
                                 request_absolute_uri(*request,
                                                      request2.translate.response->scheme,
                                                      request2.translate.response->host,
                                                      uri),
                                 &request2.uri,
                                 request2.args,
                                 request2.session_cookie,
                                 request2.session_id,
                                 HTTP_METHOD_GET, request->headers);

    body = css_processor(request->pool, body,
                         widget, &request2.env,
                         transformation.u.css_processor.options);
    assert(body != nullptr);

    response_headers = processor_header_forward(request->pool,
                                                response_headers);

    response_handler.InvokeResponse(&request2, status, response_headers, body);
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
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Empty template cannot be processed");
        return;
    }

    if (!text_processor_allowed(response_headers)) {
        istream_close_unused(body);
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Invalid template content type");
        return;
    }

    struct widget *widget = NewFromPool<struct widget>(*request->pool);
    widget->InitRoot(*request->pool,
                     strref_dup(request->pool, &request2.uri.base));

    if (request2.translate.response->untrusted != nullptr) {
        daemon_log(2, "refusing to render template on untrusted domain '%s'\n",
                   request2.translate.response->untrusted);
        istream_close_unused(body);
        response_dispatch_message(request2, HTTP_STATUS_FORBIDDEN,
                                  "Forbidden");
        return;
    }

    const char *uri = request2.translate.response->uri != nullptr
        ? request2.translate.response->uri
        : request->uri;

    if (request2.translate.response->uri != nullptr)
        strref_set_c(&request2.uri.base, request2.translate.response->uri);

    request2.env = processor_env(request->pool,
                                 request2.translate.response->site,
                                 request2.translate.response->untrusted,
                                 request->local_host_and_port, request->remote_host,
                                 uri,
                                 request_absolute_uri(*request,
                                                      request2.translate.response->scheme,
                                                      request2.translate.response->host,
                                                      uri),
                                 &request2.uri,
                                 request2.args,
                                 request2.session_cookie,
                                 request2.session_id,
                                 HTTP_METHOD_GET, request->headers);

    body = text_processor(request->pool, body,
                          widget, &request2.env);
    assert(body != nullptr);

    response_headers = processor_header_forward(request->pool,
                                                response_headers);

    response_handler.InvokeResponse(&request2, status, response_headers, body);
}

/**
 * Append response headers set by the translation server.
 */
static void
translation_response_headers(GrowingBuffer &headers,
                             const TranslateResponse &tr)
{
    if (tr.www_authenticate != nullptr)
        header_write(&headers, "www-authenticate", tr.www_authenticate);

    if (tr.authentication_info != nullptr)
        header_write(&headers, "authentication-info", tr.authentication_info);

    for (const auto &i : tr.response_headers)
        header_write(&headers, i.key, i.value);
}

/**
 * Generate additional response headers as needed.
 */
static void
more_response_headers(const request &request2, HttpHeaders &headers)
{
    GrowingBuffer &headers2 =
        headers.MakeBuffer(*request2.request->pool, 256);

    /* RFC 2616 3.8: Product Tokens */
    header_write(&headers2, "server", request2.product_token != nullptr
                 ? request2.product_token
                 : BRIEF_PRODUCT_TOKEN);

#ifndef NO_DATE_HEADER
    /* RFC 2616 14.18: Date */
    header_write(&headers2, "date", request2.date != nullptr
                 ? request2.date
                 : http_date_format(time(nullptr)));
#endif

    translation_response_headers(headers2, *request2.translate.response);
}

/**
 * Generate the Set-Cookie response header for the given request.
 */
static void
response_generate_set_cookie(request &request2, GrowingBuffer &headers)
{
    assert(!request2.stateless);
    assert(request2.session_cookie != nullptr);

    if (request2.send_session_cookie) {
        header_write_begin(&headers, "set-cookie");
        growing_buffer_write_string(&headers, request2.session_cookie);
        growing_buffer_write_buffer(&headers, "=", 1);
        growing_buffer_write_string(&headers,
                                    request2.session_id.Format(request2.session_id_string));
        growing_buffer_write_string(&headers, "; HttpOnly; Path=");

        const char *cookie_path = request2.translate.response->cookie_path;
        if (cookie_path == nullptr)
            cookie_path = "/";

        growing_buffer_write_string(&headers, cookie_path);
        growing_buffer_write_string(&headers, "; Version=1");

        if (request2.translate.response->secure_cookie)
            growing_buffer_write_string(&headers, "; Secure");

        if (request2.translate.response->cookie_domain != nullptr) {
            growing_buffer_write_string(&headers, "; Domain=\"");
            growing_buffer_write_string(&headers,
                                        request2.translate.response->cookie_domain);
            growing_buffer_write_string(&headers, "\"");
        }

        /* "Discard" must be last, to work around an Android bug*/
        growing_buffer_write_string(&headers, "; Discard");

        header_write_finish(&headers);

        /* workaround for IE10 bug; see
           http://projects.intern.cm-ag/view.php?id=3789 for
           details */
        header_write(&headers, "p3p", "CP=\"CAO PSA OUR\"");

        auto *session = request_make_session(request2);
        if (session != nullptr) {
            session->cookie_sent = true;
            session_put(session);
        }
    } else if (request2.translate.response->discard_session &&
               !request2.session_id.IsDefined()) {
        /* delete the cookie for the discarded session */
        header_write_begin(&headers, "set-cookie");
        growing_buffer_write_string(&headers, request2.session_cookie);
        growing_buffer_write_string(&headers, "=; HttpOnly; Path=");

        const char *cookie_path = request2.translate.response->cookie_path;
        if (cookie_path == nullptr)
            cookie_path = "/";

        growing_buffer_write_string(&headers, cookie_path);
        growing_buffer_write_string(&headers, "; Version=1; Max-Age=0");

        if (request2.translate.response->cookie_domain != nullptr) {
            growing_buffer_write_string(&headers, "; Domain=\"");
            growing_buffer_write_string(&headers,
                                        request2.translate.response->cookie_domain);
            growing_buffer_write_string(&headers, "\"");
        }

        /* "Discard" must be last, to work around an Android bug*/
        growing_buffer_write_string(&headers, "; Discard");

        header_write_finish(&headers);
    }
}

/*
 * dispatch
 *
 */

static void
response_dispatch_direct(request &request2,
                         http_status_t status, HttpHeaders &&headers,
                         struct istream *body)
{
    assert(!request2.response_sent);
    assert(body == nullptr || !istream_has_handler(body));

    struct pool &pool = *request2.request->pool;

    if (http_status_is_success(status) &&
        request2.translate.response->www_authenticate != nullptr)
        /* default to "401 Unauthorized" */
        status = HTTP_STATUS_UNAUTHORIZED;

    more_response_headers(request2, headers);

    request_discard_body(request2);

    if (!request2.stateless)
        response_generate_set_cookie(request2, headers.MakeBuffer(pool, 512));

#ifdef SPLICE
    if (body != nullptr)
        body = istream_pipe_new(request2.request->pool, body,
                                global_pipe_stock);
#endif

#ifndef NDEBUG
    request2.response_sent = true;
#endif

    http_server_response(request2.request, status,
                         std::move(headers),
                         body);
}

static void
response_apply_filter(request &request2,
                      http_status_t status, struct strmap *headers2,
                      struct istream *body,
                      const struct resource_address &filter)
{
    struct http_server_request *request = request2.request;
    const char *source_tag;

    source_tag = resource_tag_append_etag(request->pool,
                                          request2.resource_tag, headers2);
    request2.resource_tag = source_tag != nullptr
        ? p_strcat(request->pool, source_tag, "|",
                   resource_address_id(&filter, request->pool),
                   nullptr)
        : nullptr;

#ifdef SPLICE
    if (body != nullptr)
        body = istream_pipe_new(request->pool, body, global_pipe_stock);
#endif

    filter_cache_request(global_filter_cache, request->pool, &filter,
                         source_tag, status, headers2, body,
                         &response_handler, &request2,
                         &request2.async_ref);
}

static void
response_apply_transformation(request &request2,
                              http_status_t status, struct strmap *headers,
                              struct istream *body,
                              const Transformation &transformation)
{
    request2.transformed = true;

    switch (transformation.type) {
    case Transformation::Type::FILTER:
        response_apply_filter(request2, status, headers, body,
                              transformation.u.filter);
        break;

    case Transformation::Type::PROCESS:
        /* processor responses cannot be cached */
        request2.resource_tag = nullptr;

        response_invoke_processor(request2, status, headers, body,
                                  transformation);
        break;

    case Transformation::Type::PROCESS_CSS:
        /* processor responses cannot be cached */
        request2.resource_tag = nullptr;

        response_invoke_css_processor(request2, status, headers, body,
                                      transformation);

    case Transformation::Type::PROCESS_TEXT:
        /* processor responses cannot be cached */
        request2.resource_tag = nullptr;

        response_invoke_text_processor(request2, status, headers, body);
    }
}

static bool
filter_enabled(const TranslateResponse &tr,
               http_status_t status)
{
    return http_status_is_success(status) ||
        (http_status_is_client_error(status) && tr.filter_4xx);
}

void
response_dispatch(struct request &request2,
                  http_status_t status, HttpHeaders &&headers,
                  struct istream *body)
{
    assert(!request2.response_sent);
    assert(body == nullptr || !istream_has_handler(body));

    if (http_status_is_error(status) && !request2.transformed &&
        !request2.translate.response->error_document.IsNull()) {
        request2.transformed = true;

        /* for sure, the errdoc library doesn't use the request body;
           discard it as early as possible */
        request_discard_body(request2);

        errdoc_dispatch_response(request2, status,
                                 request2.translate.response->error_document,
                                 std::move(headers), body);
        return;
    }

    /* if HTTP status code is not successful: don't apply
       transformation on the error document */
    const Transformation *transformation = request2.PopTransformation();
    if (transformation != nullptr &&
        filter_enabled(*request2.translate.response, status)) {
        response_apply_transformation(request2, status,
                                      &headers.ToMap(*request2.request->pool),
                                      body,
                                      *transformation);
    } else
        response_dispatch_direct(request2, status, std::move(headers), body);
}

void
response_dispatch_message2(struct request &request2, http_status_t status,
                           HttpHeaders &&headers, const char *msg)
{
    struct pool *pool = request2.request->pool;

    assert(http_status_is_valid(status));
    assert(msg != nullptr);

    headers.Write(*pool, "content-type", "text/plain");

    response_dispatch(request2, status, std::move(headers),
                      istream_string_new(pool, msg));
}

void
response_dispatch_message(struct request &request2, http_status_t status,
                          const char *msg)
{
    response_dispatch_message2(request2, status, HttpHeaders(), msg);
}

void
response_dispatch_redirect(struct request &request2, http_status_t status,
                           const char *location, const char *msg)
{
    struct pool *pool = request2.request->pool;

    assert(status >= 300 && status < 400);
    assert(location != nullptr);

    if (msg == nullptr)
        msg = "redirection";

    HttpHeaders headers;
    headers.Write(*pool, "location", location);

    response_dispatch_message2(request2, status, std::move(headers), msg);
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

    assert(!request2.response_sent);
    assert(body == nullptr || !istream_has_handler(body));

    if (http_status_is_success(status)) {
        if (!request2.transformed &&
            (request2.translate.response->response_header_forward.modes[HEADER_GROUP_TRANSFORMATION] == HEADER_FORWARD_MANGLE)) {
            /* handle the response header "x-cm4all-view" */
            const char *view_name = headers->Get("x-cm4all-view");
            if (view_name != nullptr) {
                const WidgetView *view =
                    widget_view_lookup(request2.translate.response->views,
                                       view_name);
                if (view == nullptr) {
                    /* the view specified in the response header does not
                       exist, bail out */

                    if (body != nullptr)
                        istream_close_unused(body);

                    daemon_log(4, "No such view: %s\n", view_name);
                    response_dispatch_message(request2, HTTP_STATUS_NOT_FOUND,
                                              "No such view");
                    return;
                }

                request2.translate.transformation = view->transformation;
            }
        }

        const Transformation *transformation = request2.PopTransformation();
        if (transformation != nullptr) {
            response_apply_transformation(request2, status, headers, body,
                                          *transformation);
            return;
        }
    }

    const struct strmap *original_headers = headers;

    headers = forward_response_headers(*request->pool, status, headers,
                                       request->local_host_and_port,
                                       request2.session_cookie,
                                       request2.translate.response->response_header_forward);

    headers = add_translation_vary_header(request->pool, headers,
                                          request2.translate.response);

    request2.product_token = headers->Remove("server");

#ifdef NO_DATE_HEADER
    request2.date = headers->Remove("date");
#endif

    HttpHeaders headers2(headers);

    if (original_headers != nullptr && request->method == HTTP_METHOD_HEAD)
        /* pass Content-Length, even though there is no response body
           (RFC 2616 14.13) */
        headers2.MoveToBuffer(*request->pool, "content-length");

    response_dispatch(request2,
                      status, std::move(headers2),
                      body);
}

static void
response_abort(GError *error, void *ctx)
{
    request &request2 = *(request *)ctx;

    assert(!request2.response_sent);

    daemon_log(2, "error on %s: %s\n", request2.request->uri, error->message);

    response_dispatch_error(request2, error);

    g_error_free(error);
}

const struct http_response_handler response_handler = {
    .response = response_response,
    .abort = response_abort,
};
