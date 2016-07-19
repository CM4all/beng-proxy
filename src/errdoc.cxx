/*
 * Error document handler.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "errdoc.hxx"
#include "request.hxx"
#include "bp_instance.hxx"
#include "http_server/Request.hxx"
#include "http_headers.hxx"
#include "http_response.hxx"
#include "tcache.hxx"
#include "TranslateHandler.hxx"
#include "ResourceLoader.hxx"
#include "http_response.hxx"
#include "istream/istream.hxx"
#include "istream/istream_hold.hxx"

#include <glib.h>

#include <daemon/log.h>

struct ErrorResponseLoader final : HttpResponseHandler, Cancellable {
    CancellablePointer cancel_ptr;

    Request *request2;

    http_status_t status;
    HttpHeaders headers;
    Istream *body;

    TranslateRequest translate_request;

    ErrorResponseLoader(Request &_request, http_status_t _status,
                        HttpHeaders &&_headers, Istream *_body)
        :request2(&_request), status(_status),
         headers(std::move(_headers)),
         body(_body != nullptr
              ? istream_hold_new(request2->pool, *_body)
              : nullptr) {}

    /* virtual methods from class Cancellable */
    void Cancel() override;

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(GError *error) override;
};

static void
errdoc_resubmit(ErrorResponseLoader &er)
{
    response_dispatch(*er.request2, er.status, std::move(er.headers), er.body);
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
        if (body != nullptr)
            /* close the original (error) response body */
            body->CloseUnused();

        request2->InvokeResponse(status, std::move(_headers), _body);
    } else {
        if (_body != nullptr)
            /* discard the error document response */
            body->CloseUnused();

        errdoc_resubmit(*this);
    }
}

void
ErrorResponseLoader::OnHttpError(GError *error)
{
    daemon_log(2, "error on error document of %s: %s\n",
               request2->request.uri, error->message);
    g_error_free(error);

    errdoc_resubmit(*this);
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
    } else
        errdoc_resubmit(er);
}

static void
errdoc_translate_error(GError *error, void *ctx)
{
    auto &er = *(ErrorResponseLoader *)ctx;

    daemon_log(2, "error document translation error: %s\n", error->message);
    g_error_free(error);

    errdoc_resubmit(er);
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
    if (body != nullptr)
        body->CloseUnused();

    cancel_ptr.Cancel();
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
