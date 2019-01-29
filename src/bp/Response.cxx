/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Utilities for transforming the HTTP response being sent.
 */

#include "Request.hxx"
#include "Connection.hxx"
#include "ProxyWidget.hxx"
#include "Instance.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_headers.hxx"
#include "http_util.hxx"
#include "header_writer.hxx"
#include "header_parser.hxx"
#include "ForwardHeaders.hxx"
#include "widget/Widget.hxx"
#include "widget/Ref.hxx"
#include "widget/Class.hxx"
#include "widget/Dump.hxx"
#include "session/Session.hxx"
#include "GrowingBuffer.hxx"
#include "ResourceLoader.hxx"
#include "resource_tag.hxx"
#include "hostname.hxx"
#include "errdoc.hxx"
#include "strmap.hxx"
#include "pheaders.hxx"
#include "XmlProcessor.hxx"
#include "CssProcessor.hxx"
#include "TextProcessor.hxx"
#include "istream/istream.hxx"
#include "istream/istream_deflate.hxx"
#include "istream/AutoPipeIstream.hxx"
#include "istream/YamlSubstIstream.hxx"
#include "istream/istream_string.hxx"
#include "pool/pool.hxx"
#include "translation/Vary.hxx"
#include "translation/Transformation.hxx"
#include "http/Date.hxx"
#include "product.h"
#include "http_address.hxx"
#include "relocate_uri.hxx"
#include "FilterStatus.hxx"

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

    return p_strcat(&request.pool,
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

inline UnusedIstreamPtr
Request::AutoDeflate(HttpHeaders &response_headers,
                     UnusedIstreamPtr response_body)
{
    if (compressed) {
        /* already compressed */
    } else if (response_body &&
               translate.response->auto_deflate &&
        http_client_accepts_encoding(request.headers, "deflate") &&
        response_headers.Get("content-encoding") == nullptr) {
        auto available = response_body.GetAvailable(false);
        if (available < 0 || available >= 512) {
            compressed = true;
            response_headers.Write("content-encoding", "deflate");
            response_body = istream_deflate_new(pool, std::move(response_body),
                                                instance.event_loop);
        }
    } else if (response_body &&
               translate.response->auto_gzip &&
        http_client_accepts_encoding(request.headers, "gzip") &&
        response_headers.Get("content-encoding") == nullptr) {
        auto available = response_body.GetAvailable(false);
        if (available < 0 || available >= 512) {
            compressed = true;
            response_headers.Write("content-encoding", "gzip");
            response_body = istream_deflate_new(pool, std::move(response_body),
                                                instance.event_loop, true);
        }
    }

    return response_body;
}

/*
 * processor invocation
 *
 */

inline void
Request::InvokeXmlProcessor(http_status_t status,
                            StringMap &response_headers,
                            UnusedIstreamPtr response_body,
                            const Transformation &transformation)
{
    const char *uri;

    assert(!response_sent);

    if (!response_body) {
        DispatchResponse(HTTP_STATUS_BAD_GATEWAY,
                         "Empty template cannot be processed");
        return;
    }

    if (!processable(response_headers)) {
        response_body.Clear();
        DispatchResponse(HTTP_STATUS_BAD_GATEWAY,
                         "Invalid template content type");
        return;
    }

    auto *widget = NewFromPool<Widget>(pool,
                                       Widget::RootTag(),
                                       pool,
                                       translate.response->uri != nullptr
                                       ? translate.response->uri
                                       : p_strdup(pool, dissected_uri.base));

    const struct widget_ref *focus_ref =
        widget_ref_parse(&pool, args.Remove("focus"));

    const struct widget_ref *proxy_ref =
        widget_ref_parse(&pool, args.Get("frame"));

    if (focus_ref != nullptr && proxy_ref != nullptr &&
        !widget_ref_includes(proxy_ref, focus_ref)) {
        /* the focused widget is not reachable because it is not
           within the "frame" */

        focus_ref = nullptr;

        if (request_body) {
            logger(4, "discarding non-framed request body");
            request_body.Clear();
        }
    }

    widget->from_request.focus_ref = focus_ref;

    if (proxy_ref != nullptr)
        /* disable all following transformations, because we're doing
           a direct proxy request to a widget */
        CancelTransformations();

    if (translate.response->untrusted != nullptr && proxy_ref == nullptr) {
        logger(2, "refusing to render template on untrusted domain '",
               translate.response->untrusted, "'");
        response_body.Clear();
        DispatchResponse(HTTP_STATUS_FORBIDDEN, "Forbidden");
        return;
    }

    if (request_body &&
        widget->from_request.focus_ref != nullptr)
        widget->for_focused.body = std::move(request_body);

    uri = translate.response->uri != nullptr
        ? translate.response->uri
        : request.uri;

    if (translate.response->uri != nullptr)
        dissected_uri.base = translate.response->uri;

    /* make sure we have a session */
    {
        auto session = MakeRealmSession();
        if (session) {
            if (widget->from_request.focus_ref == nullptr)
                /* drop the widget session and all descendants if there is
                   no focus */
                session_drop_widgets(*session, widget->id, proxy_ref);
        }
    }

    http_method_t method = request.method;
    if (http_method_is_empty(method) && HasTransformations())
        /* the following transformation may need the processed
           document to generate its headers, so we should not pass
           HEAD to the processor */
        method = HTTP_METHOD_GET;

    widget->for_focused.method = method;
    widget->for_focused.path_info = args.Remove("path");
    widget->for_focused.query_string = dissected_uri.query;

    env = processor_env(instance.event_loop,
                        *instance.cached_resource_loader,
                        *instance.buffered_filter_resource_loader,
                        connection.per_request.site_name,
                        translate.response->untrusted,
                        request.local_host_and_port, request.remote_host,
                        uri,
                        request_absolute_uri(request,
                                             translate.response->scheme,
                                             translate.response->host,
                                             uri),
                        dissected_uri.base,
                        &args,
                        session_cookie,
                        session_id, realm,
                        &request.headers);

    if (proxy_ref != nullptr) {
        /* the client requests a widget in proxy mode */

        proxy_widget(*this, std::move(response_body),
                     *widget, proxy_ref, transformation.u.processor.options);
    } else {
        /* the client requests the whole template */
        response_body = processor_process(pool, std::move(response_body),
                                          *widget, env,
                                          transformation.u.processor.options);
        assert(response_body);

        if (instance.config.dump_widget_tree)
            response_body = widget_dump_tree_after_istream(pool,
                                                           std::move(response_body),
                                                           *widget);

        InvokeResponse(status,
                       processor_header_forward(pool, response_headers),
                       std::move(response_body));
    }
}

static bool
css_processable(const StringMap &headers)
{
    const char *content_type = headers.Get("content-type");
    return content_type != nullptr &&
        strncmp(content_type, "text/css", 8) == 0;
}

inline void
Request::InvokeCssProcessor(http_status_t status,
                            StringMap &response_headers,
                            UnusedIstreamPtr response_body,
                            const Transformation &transformation)
{
    assert(!response_sent);

    if (!response_body) {
        DispatchResponse(HTTP_STATUS_BAD_GATEWAY,
                         "Empty template cannot be processed");
        return;
    }

    if (!css_processable(response_headers)) {
        response_body.Clear();
        DispatchResponse(HTTP_STATUS_BAD_GATEWAY,
                         "Invalid template content type");
        return;
    }

    auto *widget = NewFromPool<Widget>(pool,
                                       Widget::RootTag(),
                                       pool,
                                       p_strdup(pool, dissected_uri.base));

    if (translate.response->untrusted != nullptr) {
        logger(2, "refusing to render template on untrusted domain '",
               translate.response->untrusted, "'");
        response_body.Clear();
        DispatchResponse(HTTP_STATUS_FORBIDDEN, "Forbidden");
        return;
    }

    const char *uri = translate.response->uri != nullptr
        ? translate.response->uri
        : request.uri;

    if (translate.response->uri != nullptr)
        dissected_uri.base = translate.response->uri;

    env = processor_env(instance.event_loop,
                        *instance.cached_resource_loader,
                        *instance.buffered_filter_resource_loader,
                        translate.response->site,
                        translate.response->untrusted,
                        request.local_host_and_port, request.remote_host,
                        uri,
                        request_absolute_uri(request,
                                             translate.response->scheme,
                                             translate.response->host,
                                             uri),
                        dissected_uri.base,
                        &args,
                        session_cookie,
                        session_id, realm,
                        &request.headers);

    response_body = css_processor(pool, std::move(response_body),
                                  *widget, env,
                                  transformation.u.css_processor.options);
    assert(response_body);

    InvokeResponse(status,
                   processor_header_forward(pool,
                                            response_headers),
                   std::move(response_body));
}

inline void
Request::InvokeTextProcessor(http_status_t status,
                             StringMap &response_headers,
                             UnusedIstreamPtr response_body)
{
    assert(!response_sent);

    if (!response_body) {
        DispatchResponse(HTTP_STATUS_BAD_GATEWAY,
                         "Empty template cannot be processed");
        return;
    }

    if (!text_processor_allowed(response_headers)) {
        response_body.Clear();
        DispatchResponse(HTTP_STATUS_BAD_GATEWAY,
                         "Invalid template content type");
        return;
    }

    auto *widget = NewFromPool<Widget>(pool,
                                       Widget::RootTag(),
                                       pool,
                                       p_strdup(pool,
                                                dissected_uri.base));

    if (translate.response->untrusted != nullptr) {
        logger(2, "refusing to render template on untrusted domain '",
               translate.response->untrusted, "'");
        response_body.Clear();
        DispatchResponse(HTTP_STATUS_FORBIDDEN, "Forbidden");
        return;
    }

    const char *uri = translate.response->uri != nullptr
        ? translate.response->uri
        : request.uri;

    if (translate.response->uri != nullptr)
        dissected_uri.base = translate.response->uri;

    env = processor_env(instance.event_loop,
                        *instance.cached_resource_loader,
                        *instance.buffered_filter_resource_loader,
                        translate.response->site,
                        translate.response->untrusted,
                        request.local_host_and_port, request.remote_host,
                        uri,
                        request_absolute_uri(request,
                                             translate.response->scheme,
                                             translate.response->host,
                                             uri),
                        dissected_uri.base,
                        &args,
                        session_cookie,
                        session_id, realm,
                        &request.headers);

    response_body = text_processor(pool, std::move(response_body),
                                   *widget, env);
    assert(response_body);

    InvokeResponse(status,
                   processor_header_forward(pool, response_headers),
                   std::move(response_body));
}

inline void
Request::InvokeSubst(http_status_t status,
                     StringMap &&response_headers,
                     UnusedIstreamPtr response_body,
                     bool alt_syntax,
                     const char *prefix,
                     const char *yaml_file,
                     const char *yaml_map_path) noexcept
{
    try {
        InvokeResponse(status, std::move(response_headers),
                       NewYamlSubstIstream(pool, std::move(response_body),
                                           alt_syntax,
                                           prefix, yaml_file, yaml_map_path));
    } catch (...) {
        LogDispatchError(std::current_exception());
    }
}

/**
 * Append response headers set by the translation server.
 */
static void
translation_response_headers(HttpHeaders &headers,
                             const TranslateResponse &tr)
{
    if (tr.response_header_forward[BengProxy::HeaderGroup::AUTH] == BengProxy::HeaderForwardMode::MANGLE) {
        if (tr.www_authenticate != nullptr)
            headers.Write("www-authenticate", tr.www_authenticate);

        if (tr.authentication_info != nullptr)
            headers.Write("authentication-info", tr.authentication_info);
    }

    for (const auto &i : tr.response_headers)
        headers.Write(i.key, i.value);
}

inline void
Request::MoreResponseHeaders(HttpHeaders &headers) const noexcept
{
    /* RFC 2616 3.8: Product Tokens */
    headers.Write("server", product_token != nullptr
                  ? product_token
                  : BRIEF_PRODUCT_TOKEN);

#ifndef NO_DATE_HEADER
    /* RFC 2616 14.18: Date */
    headers.Write("date", date != nullptr
                  ? date
                  : http_date_format(instance.event_loop.SystemNow()));
#endif

    translation_response_headers(headers, *translate.response);
}

inline void
Request::GenerateSetCookie(GrowingBuffer &headers)
{
    assert(!stateless);
    assert(session_cookie != nullptr);

    if (send_session_cookie) {
        header_write_begin(headers, "set-cookie");
        headers.Write(session_cookie);
        headers.Write("=", 1);
        headers.Write(session_id.Format());
        headers.Write("; HttpOnly; Path=");

        const char *cookie_path = translate.response->cookie_path;
        if (cookie_path == nullptr)
            cookie_path = "/";

        headers.Write(cookie_path);
        headers.Write("; Version=1");

        if (translate.response->secure_cookie)
            headers.Write("; Secure");

        if (translate.response->cookie_domain != nullptr) {
            headers.Write("; Domain=\"");
            headers.Write(translate.response->cookie_domain);
            headers.Write("\"");
        }

        /* "Discard" must be last, to work around an Android bug*/
        headers.Write("; Discard");

        header_write_finish(headers);

        /* workaround for IE10 bug; see
           http://projects.intern.cm-ag/view.php?id=3789 for
           details */
        header_write(headers, "p3p", "CP=\"CAO PSA OUR\"");

        auto session = MakeSession();
        if (session)
            session->cookie_sent = true;
    } else if (translate.response->discard_session &&
               !session_id.IsDefined()) {
        /* delete the cookie for the discarded session */
        header_write_begin(headers, "set-cookie");
        headers.Write(session_cookie);
        headers.Write("=; HttpOnly; Path=");

        const char *cookie_path = translate.response->cookie_path;
        if (cookie_path == nullptr)
            cookie_path = "/";

        headers.Write(cookie_path);
        headers.Write("; Version=1; Max-Age=0");

        if (translate.response->cookie_domain != nullptr) {
            headers.Write("; Domain=\"");
            headers.Write(translate.response->cookie_domain);
            headers.Write("\"");
        }

        /* "Discard" must be last, to work around an Android bug*/
        headers.Write("; Discard");

        header_write_finish(headers);
    }
}

/*
 * dispatch
 *
 */

inline void
Request::DispatchResponseDirect(http_status_t status, HttpHeaders &&headers,
                                UnusedIstreamPtr body)
{
    assert(!response_sent);

    if (http_status_is_success(status) &&
        translate.response->www_authenticate != nullptr)
        /* default to "401 Unauthorized" */
        status = HTTP_STATUS_UNAUTHORIZED;

    MoreResponseHeaders(headers);

    DiscardRequestBody();

    if (!stateless)
        GenerateSetCookie(headers.GetBuffer());

#ifdef SPLICE
    if (body)
        body = NewAutoPipeIstream(&pool, std::move(body), instance.pipe_stock);
#endif

#ifndef NDEBUG
    response_sent = true;
#endif

    http_server_response(&request, status,
                         std::move(headers),
                         std::move(body));
}

inline void
Request::ApplyFilter(http_status_t status, StringMap &&headers2,
                      UnusedIstreamPtr body,
                      const ResourceAddress &filter, bool reveal_user) noexcept
{
    previous_status = status;

    const char *source_tag = resource_tag_append_etag(&pool, resource_tag,
                                                      headers2);
    resource_tag = source_tag != nullptr
        ? p_strcat(&pool, source_tag, "|", filter.GetId(pool), nullptr)
        : nullptr;

    if (reveal_user)
        forward_reveal_user(headers2, GetRealmSession().get());

#ifdef SPLICE
    if (body)
        body = NewAutoPipeIstream(&pool, std::move(body), instance.pipe_stock);
#endif

    instance.buffered_filter_resource_loader
        ->SendRequest(pool, session_id.GetClusterHash(),
                      translate.response->site,
                      HTTP_METHOD_POST, filter, status, std::move(headers2),
                      std::move(body), source_tag,
                      *this, cancel_ptr);
}

void
Request::ApplyTransformation(http_status_t status, StringMap &&headers,
                             UnusedIstreamPtr response_body,
                             const Transformation &transformation)
{
    transformed = true;

    switch (transformation.type) {
    case Transformation::Type::FILTER:
        ApplyFilter(status, std::move(headers),
                    std::move(response_body),
                    transformation.u.filter.address,
                    transformation.u.filter.reveal_user);
        break;

    case Transformation::Type::PROCESS:
        /* processor responses cannot be cached */
        resource_tag = nullptr;

        InvokeXmlProcessor(status, headers, std::move(response_body),
                           transformation);
        break;

    case Transformation::Type::PROCESS_CSS:
        /* processor responses cannot be cached */
        resource_tag = nullptr;

        InvokeCssProcessor(status, headers, std::move(response_body),
                           transformation);
        break;

    case Transformation::Type::PROCESS_TEXT:
        /* processor responses cannot be cached */
        resource_tag = nullptr;

        InvokeTextProcessor(status, headers, std::move(response_body));
        break;

    case Transformation::Type::SUBST:
        /* subst responses cannot be cached */
        resource_tag = nullptr;

        InvokeSubst(status, std::move(headers), std::move(response_body),
                    translate.response->subst_alt_syntax,
                    transformation.u.subst.prefix,
                    transformation.u.subst.yaml_file,
                    transformation.u.subst.yaml_map_path);
        break;
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
Request::DispatchResponse(http_status_t status, HttpHeaders &&headers,
                          UnusedIstreamPtr response_body)
{
    assert(!response_sent);

    if (http_status_is_error(status) && !transformed &&
        !translate.response->error_document.IsNull()) {
        transformed = true;

        /* for sure, the errdoc library doesn't use the request body;
           discard it as early as possible */
        DiscardRequestBody();

        errdoc_dispatch_response(*this, status,
                                 translate.response->error_document,
                                 std::move(headers),
                                 std::move(response_body));
        return;
    }

    /* if HTTP status code is not successful: don't apply
       transformation on the error document */
    const Transformation *transformation = PopTransformation();
    if (transformation != nullptr &&
        filter_enabled(*translate.response, status)) {
        ApplyTransformation(status, std::move(headers).ToMap(),
                            std::move(response_body),
                            *transformation);
    } else {
        response_body = AutoDeflate(headers, std::move(response_body));
        DispatchResponseDirect(status, std::move(headers),
                               std::move(response_body));
    }
}

void
Request::DispatchResponse(http_status_t status,
                          HttpHeaders &&headers, const char *msg)
{
    assert(http_status_is_valid(status));
    assert(msg != nullptr);

    headers.Write("content-type", "text/plain");

    DispatchResponse(status, std::move(headers),
                     istream_string_new(pool, msg));
}

void
Request::DispatchResponse(http_status_t status, const char *msg)
{
    DispatchResponse(status, HttpHeaders(pool), msg);
}

void
Request::DispatchRedirect(http_status_t status,
                          const char *location, const char *msg)
{
    assert(status >= 300 && status < 400);
    assert(location != nullptr);

    if (msg == nullptr)
        msg = "redirection";

    HttpHeaders headers(pool);
    headers.Write("location", location);

    DispatchResponse(status, std::move(headers), msg);
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

    const char *new_uri = RelocateUri(request.request.pool, uri,
                                      address.host_and_port,
                                      internal_path,
                                      external_scheme, external_host,
                                      request.dissected_uri.base, tr.base);
    if (new_uri == nullptr)
        return uri;

    // TODO: check regex and inverse_regex

    return new_uri;
}

/*
 * HTTP response handler
 *
 */

void
Request::OnHttpResponse(http_status_t status, StringMap &&headers,
                        UnusedIstreamPtr body) noexcept
{
    assert(!response_sent);

    if (previous_status != http_status_t(0)) {
        status = ApplyFilterStatus(previous_status, status, !!body);
        previous_status = http_status_t(0);
    }

    if (collect_cookies) {
        collect_cookies = false;
        CollectCookies(headers);
    }

    if (http_status_is_success(status)) {
        using namespace BengProxy;

        if (!transformed &&
            (translate.response->response_header_forward[HeaderGroup::TRANSFORMATION] == HeaderForwardMode::MANGLE)) {
            /* handle the response header "x-cm4all-view" */
            const char *view_name = headers.Get("x-cm4all-view");
            if (view_name != nullptr) {
                const WidgetView *view =
                    widget_view_lookup(translate.response->views, view_name);
                if (view == nullptr) {
                    /* the view specified in the response header does not
                       exist, bail out */

                    body.Clear();

                    logger(4, "No such view: ", view_name);
                    DispatchResponse(HTTP_STATUS_NOT_FOUND, "No such view");
                    return;
                }

                translate.transformation = view->transformation;
            }
        }

        const Transformation *transformation = PopTransformation();
        if (transformation != nullptr) {
            ApplyTransformation(status, std::move(headers), std::move(body),
                                *transformation);
            return;
        }
    }

    const auto *original_headers = &headers;

    auto new_headers = forward_response_headers(pool, status, headers,
                                                request.local_host_and_port,
                                                session_cookie,
                                                RelocateCallback, this,
                                                translate.response->response_header_forward);

    add_translation_vary_header(new_headers,
                                *translate.response);

    product_token = new_headers.Remove("server");

#ifdef NO_DATE_HEADER
    date = new_headers.Remove("date");
#endif

    HttpHeaders headers2(std::move(new_headers));

    if (original_headers != nullptr && request.method == HTTP_METHOD_HEAD)
        /* pass Content-Length, even though there is no response body
           (RFC 2616 14.13) */
        headers2.MoveToBuffer("content-length");

    DispatchResponse(status, std::move(headers2), std::move(body));
}

void
Request::OnHttpError(std::exception_ptr ep) noexcept
{
    assert(!response_sent);

    LogDispatchError(ep);
}
