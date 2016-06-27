/*
 * Utilities for transforming the HTTP response being sent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.hxx"
#include "transformation.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_headers.hxx"
#include "http_response.hxx"
#include "http_util.hxx"
#include "header_writer.hxx"
#include "header_parser.hxx"
#include "header_forward.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "widget_dump.hxx"
#include "proxy_widget.hxx"
#include "session.hxx"
#include "growing_buffer.hxx"
#include "ResourceLoader.hxx"
#include "resource_tag.hxx"
#include "hostname.hxx"
#include "errdoc.hxx"
#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "strmap.hxx"
#include "pheaders.hxx"
#include "processor.hxx"
#include "css_processor.hxx"
#include "text_processor.hxx"
#include "istream/istream.hxx"
#include "istream/istream_deflate.hxx"
#include "istream/istream_pipe.hxx"
#include "istream/istream_string.hxx"
#include "tvary.hxx"
#include "http_date.hxx"
#include "product.h"
#include "http_address.hxx"
#include "relocate_uri.hxx"

#include <daemon/log.h>

static const char *
request_absolute_uri(const HttpServerRequest &request,
                     const char *scheme, const char *host, const char *uri)
{
    assert(uri != nullptr);

    if (scheme == nullptr)
        scheme = "http";

    if (host == nullptr)
        host = request.headers.Get("host");

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
session_drop_widgets(RealmSession &session, const char *uri,
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
            ws.Destroy(session.parent.pool);
            return;
        }

        map = &ws.children;
        id = ref->id;
        ref = ref->next;
    }
}

static Istream *
AutoDeflate(Request &request2, HttpHeaders &response_headers,
            Istream *response_body)
{
    if (request2.compressed) {
        /* already compressed */
    } else if (response_body != nullptr &&
               request2.translate.response->auto_deflate &&
        http_client_accepts_encoding(request2.request.headers, "deflate") &&
        response_headers.Get("content-encoding") == nullptr) {
        auto available = response_body->GetAvailable(false);
        if (available < 0 || available >= 512) {
            request2.compressed = true;
            response_headers.Write(request2.pool,
                                   "content-encoding", "deflate");
            response_body = istream_deflate_new(request2.pool,
                                                *response_body,
                                                request2.instance.event_loop);
        }
    } else if (response_body != nullptr &&
               request2.translate.response->auto_gzip &&
        http_client_accepts_encoding(request2.request.headers, "gzip") &&
        response_headers.Get("content-encoding") == nullptr) {
        auto available = response_body->GetAvailable(false);
        if (available < 0 || available >= 512) {
            request2.compressed = true;
            response_headers.Write(request2.pool,
                                   "content-encoding", "gzip");
            response_body = istream_deflate_new(request2.pool,
                                                *response_body,
                                                request2.instance.event_loop,
                                                true);
        }
    }

    return response_body;
}

/*
 * processor invocation
 *
 */

static void
response_invoke_processor(Request &request2,
                          http_status_t status,
                          StringMap *response_headers,
                          Istream *body,
                          const Transformation &transformation)
{
    const auto &request = request2.request;
    const char *uri;

    assert(!request2.response_sent);
    assert(body == nullptr || !body->HasHandler());

    if (body == nullptr) {
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Empty template cannot be processed");
        return;
    }

    if (!processable(response_headers)) {
        body->CloseUnused();
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Invalid template content type");
        return;
    }

    auto *widget = NewFromPool<Widget>(request2.pool);
    widget->InitRoot(request2.pool,
                     request2.translate.response->uri != nullptr
                     ? request2.translate.response->uri
                     : p_strdup(request2.pool, request2.uri.base));

    const struct widget_ref *focus_ref =
        widget_ref_parse(&request2.pool,
                         strmap_remove_checked(request2.args, "focus"));

    const struct widget_ref *proxy_ref =
        widget_ref_parse(&request2.pool,
                         strmap_get_checked(request2.args, "frame"));

    if (focus_ref != nullptr && proxy_ref != nullptr &&
        !widget_ref_includes(proxy_ref, focus_ref)) {
        /* the focused widget is not reachable because it is not
           within the "frame" */

        focus_ref = nullptr;

        if (request.body != nullptr) {
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
        body->CloseUnused();
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
        : request.uri;

    if (request2.translate.response->uri != nullptr)
        request2.uri.base = request2.translate.response->uri;

    /* make sure we have a session */
    {
        auto session = request2.MakeRealmSession();
        if (session) {
            if (widget->from_request.focus_ref == nullptr)
                /* drop the widget session and all descendants if there is
                   no focus */
                session_drop_widgets(*session, widget->id, proxy_ref);
        }
    }

    http_method_t method = request.method;
    if (http_method_is_empty(method) && request2.HasTransformations())
        /* the following transformation may need the processed
           document to generate its headers, so we should not pass
           HEAD to the processor */
        method = HTTP_METHOD_GET;

    request2.env = processor_env(&request2.pool,
                                 request2.instance.event_loop,
                                 *request2.instance.cached_resource_loader,
                                 *request2.instance.filter_resource_loader,
                                 request2.connection.site_name,
                                 request2.translate.response->untrusted,
                                 request.local_host_and_port, request.remote_host,
                                 uri,
                                 request_absolute_uri(request,
                                                      request2.translate.response->scheme,
                                                      request2.translate.response->host,
                                                      uri),
                                 &request2.uri,
                                 request2.args,
                                 request2.session_cookie,
                                 request2.session_id, request2.realm,
                                 method, &request.headers);

    if (proxy_ref != nullptr) {
        /* the client requests a widget in proxy mode */

        proxy_widget(request2, *body,
                     *widget, proxy_ref, transformation.u.processor.options);
    } else {
        /* the client requests the whole template */
        body = processor_process(request2.pool, *body,
                                 *widget, request2.env,
                                 transformation.u.processor.options);
        assert(body != nullptr);

        if (request2.instance.config.dump_widget_tree)
            body = widget_dump_tree_after_istream(request2.pool, *body,
                                                  *widget);

        response_headers = processor_header_forward(&request2.pool,
                                                    response_headers);

        response_handler.InvokeResponse(&request2, status,
                                        response_headers, body);
    }
}

static bool
css_processable(const StringMap *headers)
{
    const char *content_type;

    content_type = strmap_get_checked(headers, "content-type");
    return content_type != nullptr &&
        strncmp(content_type, "text/css", 8) == 0;
}

static void
response_invoke_css_processor(Request &request2,
                              http_status_t status,
                              StringMap *response_headers,
                              Istream *body,
                              const Transformation &transformation)
{
    const auto &request = request2.request;

    assert(!request2.response_sent);
    assert(body == nullptr || !body->HasHandler());

    if (body == nullptr) {
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Empty template cannot be processed");
        return;
    }

    if (!css_processable(response_headers)) {
        body->CloseUnused();
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Invalid template content type");
        return;
    }

    auto *widget = NewFromPool<Widget>(request2.pool);
    widget->InitRoot(request2.pool,
                     p_strdup(request2.pool, request2.uri.base));

    if (request2.translate.response->untrusted != nullptr) {
        daemon_log(2, "refusing to render template on untrusted domain '%s'\n",
                   request2.translate.response->untrusted);
        body->CloseUnused();
        response_dispatch_message(request2, HTTP_STATUS_FORBIDDEN,
                                  "Forbidden");
        return;
    }

    const char *uri = request2.translate.response->uri != nullptr
        ? request2.translate.response->uri
        : request.uri;

    if (request2.translate.response->uri != nullptr)
        request2.uri.base = request2.translate.response->uri;

    request2.env = processor_env(&request2.pool,
                                 request2.instance.event_loop,
                                 *request2.instance.cached_resource_loader,
                                 *request2.instance.filter_resource_loader,
                                 request2.translate.response->site,
                                 request2.translate.response->untrusted,
                                 request.local_host_and_port, request.remote_host,
                                 uri,
                                 request_absolute_uri(request,
                                                      request2.translate.response->scheme,
                                                      request2.translate.response->host,
                                                      uri),
                                 &request2.uri,
                                 request2.args,
                                 request2.session_cookie,
                                 request2.session_id, request2.realm,
                                 HTTP_METHOD_GET, &request.headers);

    body = css_processor(request2.pool, *body,
                         *widget, request2.env,
                         transformation.u.css_processor.options);
    assert(body != nullptr);

    response_headers = processor_header_forward(&request2.pool,
                                                response_headers);

    response_handler.InvokeResponse(&request2, status, response_headers, body);
}

static void
response_invoke_text_processor(Request &request2,
                               http_status_t status,
                               StringMap *response_headers,
                               Istream *body)
{
    const auto &request = request2.request;

    assert(!request2.response_sent);
    assert(body == nullptr || !body->HasHandler());

    if (body == nullptr) {
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Empty template cannot be processed");
        return;
    }

    if (!text_processor_allowed(response_headers)) {
        body->CloseUnused();
        response_dispatch_message(request2, HTTP_STATUS_BAD_GATEWAY,
                                  "Invalid template content type");
        return;
    }

    auto *widget = NewFromPool<Widget>(request2.pool);
    widget->InitRoot(request2.pool,
                     p_strdup(request2.pool, request2.uri.base));

    if (request2.translate.response->untrusted != nullptr) {
        daemon_log(2, "refusing to render template on untrusted domain '%s'\n",
                   request2.translate.response->untrusted);
        body->CloseUnused();
        response_dispatch_message(request2, HTTP_STATUS_FORBIDDEN,
                                  "Forbidden");
        return;
    }

    const char *uri = request2.translate.response->uri != nullptr
        ? request2.translate.response->uri
        : request.uri;

    if (request2.translate.response->uri != nullptr)
        request2.uri.base = request2.translate.response->uri;

    request2.env = processor_env(&request2.pool,
                                 request2.instance.event_loop,
                                 *request2.instance.cached_resource_loader,
                                 *request2.instance.filter_resource_loader,
                                 request2.translate.response->site,
                                 request2.translate.response->untrusted,
                                 request.local_host_and_port, request.remote_host,
                                 uri,
                                 request_absolute_uri(request,
                                                      request2.translate.response->scheme,
                                                      request2.translate.response->host,
                                                      uri),
                                 &request2.uri,
                                 request2.args,
                                 request2.session_cookie,
                                 request2.session_id, request2.realm,
                                 HTTP_METHOD_GET, &request.headers);

    body = text_processor(request2.pool, *body,
                          *widget, request2.env);
    assert(body != nullptr);

    response_headers = processor_header_forward(&request2.pool,
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
more_response_headers(const Request &request2, HttpHeaders &headers)
{
    GrowingBuffer &headers2 =
        headers.MakeBuffer(request2.pool, 256);

    /* RFC 2616 3.8: Product Tokens */
    header_write(&headers2, "server", request2.product_token != nullptr
                 ? request2.product_token
                 : BRIEF_PRODUCT_TOKEN);

#ifndef NO_DATE_HEADER
    /* RFC 2616 14.18: Date */
    header_write(&headers2, "date", request2.date != nullptr
                 ? request2.date
                 : http_date_format(std::chrono::system_clock::now()));
#endif

    translation_response_headers(headers2, *request2.translate.response);
}

/**
 * Generate the Set-Cookie response header for the given request.
 */
static void
response_generate_set_cookie(Request &request2, GrowingBuffer &headers)
{
    assert(!request2.stateless);
    assert(request2.session_cookie != nullptr);

    if (request2.send_session_cookie) {
        header_write_begin(&headers, "set-cookie");
        headers.Write(request2.session_cookie);
        headers.Write("=", 1);
        headers.Write(request2.session_id.Format(request2.session_id_string));
        headers.Write("; HttpOnly; Path=");

        const char *cookie_path = request2.translate.response->cookie_path;
        if (cookie_path == nullptr)
            cookie_path = "/";

        headers.Write(cookie_path);
        headers.Write("; Version=1");

        if (request2.translate.response->secure_cookie)
            headers.Write("; Secure");

        if (request2.translate.response->cookie_domain != nullptr) {
            headers.Write("; Domain=\"");
            headers.Write(request2.translate.response->cookie_domain);
            headers.Write("\"");
        }

        /* "Discard" must be last, to work around an Android bug*/
        headers.Write("; Discard");

        header_write_finish(&headers);

        /* workaround for IE10 bug; see
           http://projects.intern.cm-ag/view.php?id=3789 for
           details */
        header_write(&headers, "p3p", "CP=\"CAO PSA OUR\"");

        auto session = request2.MakeSession();
        if (session)
            session->cookie_sent = true;
    } else if (request2.translate.response->discard_session &&
               !request2.session_id.IsDefined()) {
        /* delete the cookie for the discarded session */
        header_write_begin(&headers, "set-cookie");
        headers.Write(request2.session_cookie);
        headers.Write("=; HttpOnly; Path=");

        const char *cookie_path = request2.translate.response->cookie_path;
        if (cookie_path == nullptr)
            cookie_path = "/";

        headers.Write(cookie_path);
        headers.Write("; Version=1; Max-Age=0");

        if (request2.translate.response->cookie_domain != nullptr) {
            headers.Write("; Domain=\"");
            headers.Write(request2.translate.response->cookie_domain);
            headers.Write("\"");
        }

        /* "Discard" must be last, to work around an Android bug*/
        headers.Write("; Discard");

        header_write_finish(&headers);
    }
}

/*
 * dispatch
 *
 */

static void
response_dispatch_direct(Request &request2,
                         http_status_t status, HttpHeaders &&headers,
                         Istream *body)
{
    assert(!request2.response_sent);
    assert(body == nullptr || !body->HasHandler());

    struct pool &pool = request2.pool;

    if (http_status_is_success(status) &&
        request2.translate.response->www_authenticate != nullptr)
        /* default to "401 Unauthorized" */
        status = HTTP_STATUS_UNAUTHORIZED;

    more_response_headers(request2, headers);

    request2.DiscardRequestBody();

    if (!request2.stateless)
        response_generate_set_cookie(request2, headers.MakeBuffer(pool, 512));

#ifdef SPLICE
    if (body != nullptr)
        body = istream_pipe_new(&request2.pool, *body,
                                request2.instance.pipe_stock);
#endif

#ifndef NDEBUG
    request2.response_sent = true;
#endif

    http_server_response(&request2.request, status,
                         std::move(headers),
                         body);
}

static void
response_apply_filter(Request &request2,
                      http_status_t status, StringMap *headers2,
                      Istream *body,
                      const ResourceAddress &filter, bool reveal_user)
{
    const char *source_tag;
    source_tag = resource_tag_append_etag(&request2.pool,
                                          request2.resource_tag, headers2);
    request2.resource_tag = source_tag != nullptr
        ? p_strcat(&request2.pool, source_tag, "|",
                   filter.GetId(request2.pool),
                   nullptr)
        : nullptr;

    if (reveal_user)
        headers2 = forward_reveal_user(request2.pool, headers2,
                                       request2.GetRealmSession().get());

#ifdef SPLICE
    if (body != nullptr)
        body = istream_pipe_new(&request2.pool, *body,
                                request2.instance.pipe_stock);
#endif

    request2.instance.filter_resource_loader
        ->SendRequest(request2.pool, request2.session_id.GetClusterHash(),
                      HTTP_METHOD_POST, filter, status, headers2,
                      body, source_tag,
                      response_handler, &request2,
                      request2.async_ref);
}

static void
response_apply_transformation(Request &request2,
                              http_status_t status, StringMap *headers,
                              Istream *body,
                              const Transformation &transformation)
{
    request2.transformed = true;

    switch (transformation.type) {
    case Transformation::Type::FILTER:
        response_apply_filter(request2, status, headers, body,
                              transformation.u.filter.address,
                              transformation.u.filter.reveal_user);
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
response_dispatch(Request &request2,
                  http_status_t status, HttpHeaders &&headers,
                  Istream *body)
{
    assert(!request2.response_sent);
    assert(body == nullptr || !body->HasHandler());

    if (http_status_is_error(status) && !request2.transformed &&
        !request2.translate.response->error_document.IsNull()) {
        request2.transformed = true;

        /* for sure, the errdoc library doesn't use the request body;
           discard it as early as possible */
        request2.DiscardRequestBody();

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
                                      &headers.ToMap(request2.pool),
                                      body,
                                      *transformation);
    } else {
        body = AutoDeflate(request2, headers, body);
        response_dispatch_direct(request2, status, std::move(headers), body);
    }
}

void
response_dispatch_message2(Request &request2, http_status_t status,
                           HttpHeaders &&headers, const char *msg)
{
    assert(http_status_is_valid(status));
    assert(msg != nullptr);

    headers.Write(request2.pool, "content-type", "text/plain");

    response_dispatch(request2, status, std::move(headers),
                      istream_string_new(&request2.pool, msg));
}

void
response_dispatch_message(Request &request2, http_status_t status,
                          const char *msg)
{
    response_dispatch_message2(request2, status, HttpHeaders(), msg);
}

void
response_dispatch_redirect(Request &request2, http_status_t status,
                           const char *location, const char *msg)
{
    assert(status >= 300 && status < 400);
    assert(location != nullptr);

    if (msg == nullptr)
        msg = "redirection";

    HttpHeaders headers;
    headers.Write(request2.pool, "location", location);

    response_dispatch_message2(request2, status, std::move(headers), msg);
}

/**
 * Callback for forward_response_headers().
 */
static const char *
RelocateCallback(const char *const uri, void *ctx)
{
    auto &request = *(Request *)ctx;
    auto &tr = *request.translate.response;

    if (tr.base == nullptr || tr.IsExpandable() || !tr.address.IsHttp())
        return uri;

    const char *external_scheme = tr.scheme != nullptr
        ? tr.scheme : "http";
    const char *external_host = tr.host != nullptr
        ? tr.host
        : request.request.headers.Get("host");

    const auto &address = tr.address.GetHttp();

    StringView internal_path = address.path;
    const char *q = internal_path.Find('?');
    if (q != nullptr)
        /* truncate the query string, because it's not part of
           request.uri.base either */
        internal_path.size = q - internal_path.data;

    const char *new_uri = RelocateUri(*request.request.pool, uri,
                                      address.host_and_port,
                                      internal_path,
                                      external_scheme, external_host,
                                      request.uri.base, tr.base);
    if (new_uri == nullptr)
        return uri;

    // TODO: check regex and inverse_regex

    return new_uri;
}

/*
 * HTTP response handler
 *
 */

static void
response_response(http_status_t status, StringMap *headers,
                  Istream *body,
                  void *ctx)
{
    auto &request2 = *(Request *)ctx;
    auto &request = request2.request;

    assert(!request2.response_sent);
    assert(body == nullptr || !body->HasHandler());

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
                        body->CloseUnused();

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

    const auto *original_headers = headers;

    headers = forward_response_headers(request2.pool, status, headers,
                                       request.local_host_and_port,
                                       request2.session_cookie,
                                       RelocateCallback, &request2,
                                       request2.translate.response->response_header_forward);

    headers = add_translation_vary_header(&request2.pool, headers,
                                          request2.translate.response);

    request2.product_token = headers->Remove("server");

#ifdef NO_DATE_HEADER
    request2.date = headers.Remove("date");
#endif

    HttpHeaders headers2(headers);

    if (original_headers != nullptr && request.method == HTTP_METHOD_HEAD)
        /* pass Content-Length, even though there is no response body
           (RFC 2616 14.13) */
        headers2.MoveToBuffer(request2.pool, "content-length");

    response_dispatch(request2,
                      status, std::move(headers2),
                      body);
}

static void
response_abort(GError *error, void *ctx)
{
    auto &request2 = *(Request *)ctx;

    assert(!request2.response_sent);

    daemon_log(2, "error on %s: %s\n", request2.request.uri, error->message);

    response_dispatch_error(request2, error);

    g_error_free(error);
}

const struct http_response_handler response_handler = {
    .response = response_response,
    .abort = response_abort,
};
