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

#include "errdoc.hxx"
#include "request.hxx"
#include "bp_instance.hxx"
#include "http_server/Request.hxx"
#include "http_headers.hxx"
#include "http_response.hxx"
#include "translation/Cache.hxx"
#include "translation/Handler.hxx"
#include "ResourceLoader.hxx"
#include "http_response.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "util/Exception.hxx"

#include <daemon/log.h>

struct ErrorResponseLoader final : HttpResponseHandler, Cancellable {
    CancellablePointer cancel_ptr;

    Request *request2;

    http_status_t status;
    HttpHeaders headers;
    UnusedHoldIstreamPtr body;

    TranslateRequest translate_request;

    ErrorResponseLoader(Request &_request, http_status_t _status,
                        HttpHeaders &&_headers, Istream *_body)
        :request2(&_request), status(_status),
         headers(std::move(_headers)),
         body(_request.pool, _body) {}

    void Destroy() {
        DeleteFromPool(request2->pool, this);
    }

    /* virtual methods from class Cancellable */
    void Cancel() override;

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(std::exception_ptr ep) override;
};

static void
errdoc_resubmit(ErrorResponseLoader &er)
{
    response_dispatch(*er.request2, er.status, std::move(er.headers),
                      er.body.Steal());
}

/*
 * HTTP response handler
 *
 */

void
ErrorResponseLoader::OnHttpResponse(http_status_t _status, StringMap &&_headers,
                                    Istream *_body)
{
    if (http_status_is_success(_status)) {
        /* close the original (error) response body */
        body.Clear();

        request2->InvokeResponse(status, std::move(_headers), _body);
    } else {
        /* close the original (error) response body */
        body.Clear();

        errdoc_resubmit(*this);
    }

    Destroy();
}

void
ErrorResponseLoader::OnHttpError(std::exception_ptr ep)
{
    daemon_log(2, "error on error document of %s: %s\n",
               request2->request.uri, GetFullMessage(ep).c_str());

    errdoc_resubmit(*this);

    Destroy();
}

/*
 * translate handler
 *
 */

static void
errdoc_translate_response(TranslateResponse &response, void *ctx)
{
    auto &er = *(ErrorResponseLoader *)ctx;

    if ((response.status == (http_status_t)0 ||
         http_status_is_success(response.status)) &&
        response.address.IsDefined()) {
        Request *request2 = er.request2;
        auto *instance = &request2->instance;

        instance->cached_resource_loader
            ->SendRequest(request2->pool, 0, HTTP_METHOD_GET,
                          response.address, HTTP_STATUS_OK,
                          StringMap(request2->pool), nullptr, nullptr,
                          er, request2->cancel_ptr);
    } else {
        errdoc_resubmit(er);
        er.Destroy();
    }
}

static void
errdoc_translate_error(std::exception_ptr ep, void *ctx)
{
    auto &er = *(ErrorResponseLoader *)ctx;

    daemon_log(2, "error document translation error: %s\n",
               GetFullMessage(ep).c_str());

    errdoc_resubmit(er);
    er.Destroy();
}

static const TranslateHandler errdoc_translate_handler = {
    .response = errdoc_translate_response,
    .error = errdoc_translate_error,
};

static void
fill_translate_request(TranslateRequest *t,
                       const TranslateRequest *src,
                       ConstBuffer<void> error_document,
                       http_status_t status)
{
    *t = *src;
    t->error_document = error_document;
    t->error_document_status = status;
}

/*
 * async operation
 *
 */

void
ErrorResponseLoader::Cancel()
{
    body.Clear();

    CancellablePointer c(std::move(cancel_ptr));
    Destroy();
    c.Cancel();
}

/*
 * constructor
 *
 */

void
errdoc_dispatch_response(Request &request2, http_status_t status,
                         ConstBuffer<void> error_document,
                         HttpHeaders &&headers, Istream *body)
{
    assert(!error_document.IsNull());

    auto *instance = &request2.instance;

    assert(instance->translate_cache != nullptr);

    auto *er = NewFromPool<ErrorResponseLoader>(request2.pool, request2,
                                                status, std::move(headers),
                                                body);

    request2.cancel_ptr = *er;

    fill_translate_request(&er->translate_request,
                           &request2.translate.request,
                           error_document, status);
    translate_cache(request2.pool, *instance->translate_cache,
                    er->translate_request,
                    errdoc_translate_handler, er,
                    er->cancel_ptr);
}
