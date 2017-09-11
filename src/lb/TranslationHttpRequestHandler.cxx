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

#include "HttpConnection.hxx"
#include "GotoConfig.hxx"
#include "Instance.hxx"
#include "Config.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "translation/Handler.hxx"
#include "translation/Response.hxx"
#include "pool.hxx"
#include "abort_close.hxx"
#include "RedirectHttps.hxx"

/*
 * TranslateHandler
 *
 */

struct LbHttpRequest {
    LbHttpConnection &connection;
    LbTranslationHandler &handler;
    HttpServerRequest &request;
    CancellablePointer &cancel_ptr;

    LbHttpRequest(LbHttpConnection &_connection,
                  LbTranslationHandler &_handler,
                  HttpServerRequest &_request,
                  CancellablePointer &_cancel_ptr)
        :connection(_connection), handler(_handler),
         request(_request), cancel_ptr(_cancel_ptr) {}
};

static void
lb_http_translate_response(TranslateResponse &response, void *ctx)
{
    auto &r = *(LbHttpRequest *)ctx;
    auto &c = r.connection;
    auto &request = r.request;

    if (response.site != nullptr)
        r.connection.per_request.site_name = p_strdup(request.pool,
                                                      response.site);

    if (response.https_only != 0 && !c.IsEncrypted()) {
        request.CheckCloseUnusedBody();

        const char *host = request.headers.Get("host");
        if (host == nullptr) {
            http_server_send_message(&request, HTTP_STATUS_BAD_REQUEST,
                                     "No Host header");
            return;
        }

        http_server_send_redirect(&request, HTTP_STATUS_MOVED_PERMANENTLY,
                                  MakeHttpsRedirect(request.pool, host,
                                                    response.https_only,
                                                    request.uri),
                                  "This page requires \"https\"");
    } else if (response.status != http_status_t(0) ||
               response.redirect != nullptr ||
        response.message != nullptr) {
        request.CheckCloseUnusedBody();

        auto status = response.status;
        if (status == http_status_t(0))
            status = HTTP_STATUS_SEE_OTHER;

        const char *body = response.message;
        if (body == nullptr)
            body = http_status_to_string(status);

        http_server_simple_response(request, status,
                                    response.redirect,
                                    body);
    } else if (response.pool != nullptr) {
        auto *destination = r.handler.FindDestination(response.pool);
        if (destination == nullptr) {
            request.CheckCloseUnusedBody();

            c.LogSendError(request,
                           std::make_exception_ptr(std::runtime_error("No such pool")));
            return;
        }

        if (response.canonical_host != nullptr)
            c.per_request.canonical_host = response.canonical_host;

        c.HandleHttpRequest(*destination, request, r.cancel_ptr);
    } else {
        request.CheckCloseUnusedBody();

        c.LogSendError(request,
                       std::make_exception_ptr(std::runtime_error("Invalid translation server response")));
    }
}

static void
lb_http_translate_error(std::exception_ptr ep, void *ctx)
{
    auto &r = *(LbHttpRequest *)ctx;

    r.request.CheckCloseUnusedBody();

    r.connection.LogSendError(r.request, ep);
}

static constexpr TranslateHandler lb_http_translate_handler = {
    .response = lb_http_translate_response,
    .error = lb_http_translate_error,
};

/*
 * constructor
 *
 */

void
LbHttpConnection::AskTranslationServer(LbTranslationHandler &handler,
                                       HttpServerRequest &request,
                                       CancellablePointer &cancel_ptr)
{
    auto *r = NewFromPool<LbHttpRequest>(request.pool, *this, handler, request,
                                         cancel_ptr);

    handler.Pick(request.pool, request,
                 listener.tag.empty() ? nullptr : listener.tag.c_str(),
                 lb_http_translate_handler, r,
                 async_optional_close_on_abort(request.pool, request.body,
                                               cancel_ptr));
}
