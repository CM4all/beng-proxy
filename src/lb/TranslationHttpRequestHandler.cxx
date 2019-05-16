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
#include "TranslationHandler.hxx"
#include "Config.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "translation/Handler.hxx"
#include "translation/Response.hxx"
#include "pool/pool.hxx"
#include "RedirectHttps.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "util/Cancellable.hxx"
#include "util/LeakDetector.hxx"

/*
 * TranslateHandler
 *
 */

struct LbHttpRequest final : private Cancellable, private LeakDetector {
    struct pool &pool;
    LbHttpConnection &connection;
    LbTranslationHandler &handler;
    HttpServerRequest &request;

    /**
     * This object temporarily holds the request body
     */
    UnusedHoldIstreamPtr request_body;

    CancellablePointer &caller_cancel_ptr;
    CancellablePointer translate_cancel_ptr;

    LbHttpRequest(LbHttpConnection &_connection,
                  LbTranslationHandler &_handler,
                  HttpServerRequest &_request,
                  CancellablePointer &_cancel_ptr)
        :pool(_request.pool), connection(_connection), handler(_handler),
         request(_request),
         request_body(request.pool, std::move(request.body)),
         caller_cancel_ptr(_cancel_ptr) {
        caller_cancel_ptr = *this;
    }

    void Destroy() {
        this->~LbHttpRequest();
    }

private:
    /* virtual methods from class Cancellable */
    void Cancel() noexcept override {
        CancellablePointer cancel_ptr(std::move(translate_cancel_ptr));
        Destroy();
        cancel_ptr.Cancel();
    }
};

static void
lb_http_translate_response(TranslateResponse &response, void *ctx)
{
    auto &r = *(LbHttpRequest *)ctx;
    auto &c = r.connection;
    auto &request = r.request;

    if (response.site != nullptr)
        c.per_request.site_name = p_strdup(request.pool, response.site);

    if (response.https_only != 0 && !c.IsEncrypted()) {
        r.Destroy();

        const char *host = c.per_request.host;
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
        r.Destroy();

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
            r.Destroy();

            c.LogSendError(request,
                           std::make_exception_ptr(std::runtime_error("No such pool")));
            return;
        }

        if (response.canonical_host != nullptr)
            c.per_request.canonical_host = response.canonical_host;

        request.body = std::move(r.request_body);

        auto &caller_cancel_ptr = r.caller_cancel_ptr;
        r.Destroy();

        c.HandleHttpRequest(*destination, request, caller_cancel_ptr);
    } else {
        r.Destroy();

        c.LogSendError(request,
                       std::make_exception_ptr(std::runtime_error("Invalid translation server response")));
    }
}

static void
lb_http_translate_error(std::exception_ptr ep, void *ctx)
{
    auto &r = *(LbHttpRequest *)ctx;
    auto &request = r.request;
    auto &connection = r.connection;

    r.Destroy();

    connection.LogSendError(request, ep);
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
                 r->translate_cancel_ptr);
}
