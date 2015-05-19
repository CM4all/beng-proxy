/*
 * Error document handler.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "errdoc.hxx"
#include "request.hxx"
#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "http_server.hxx"
#include "http_headers.hxx"
#include "http_response.hxx"
#include "tcache.hxx"
#include "get.hxx"
#include "http_response.hxx"
#include "istream.h"
#include "translate_client.hxx"

#include <daemon/log.h>

struct error_response {
    struct async_operation operation;
    struct async_operation_ref async_ref;

    struct request *request2;

    http_status_t status;
    HttpHeaders headers;
    struct istream *body;

    TranslateRequest translate_request;
};

static void
errdoc_resubmit(error_response &er)
{
    response_dispatch(*er.request2, er.status, std::move(er.headers), er.body);
}

/*
 * HTTP response handler
 *
 */

static void
errdoc_response_response(http_status_t status, struct strmap *headers,
                         struct istream *body, void *ctx)
{
    error_response &er = *(error_response *)ctx;

    if (http_status_is_success(status)) {
        if (er.body != nullptr)
            /* close the original (error) response body */
            istream_close_unused(er.body);

        response_handler.InvokeResponse(er.request2, er.status, headers, body);
    } else {
        if (body != nullptr)
            /* discard the error document response */
            istream_close_unused(body);

        errdoc_resubmit(er);
    }
}

static void
errdoc_response_abort(GError *error, void *ctx)
{
    error_response &er = *(error_response *)ctx;

    daemon_log(2, "error on error document of %s: %s\n",
               er.request2->request->uri, error->message);
    g_error_free(error);

    errdoc_resubmit(er);
}

const struct http_response_handler errdoc_response_handler = {
    .response = errdoc_response_response,
    .abort = errdoc_response_abort,
};

/*
 * translate handler
 *
 */

static void
errdoc_translate_response(TranslateResponse *response, void *ctx)
{
    error_response &er = *(error_response *)ctx;

    if ((response->status == (http_status_t)0 ||
         http_status_is_success(response->status)) &&
        response->address.type != RESOURCE_ADDRESS_NONE) {
        struct request *request2 = er.request2;
        struct pool *pool = request2->request->pool;
        struct instance *instance = request2->connection->instance;

        resource_get(instance->http_cache,
                     instance->tcp_balancer,
                     instance->lhttp_stock,
                     instance->fcgi_stock, instance->was_stock,
                     instance->delegate_stock,
                     instance->nfs_cache,
                     pool, 0, HTTP_METHOD_GET,
                     &response->address, HTTP_STATUS_OK, nullptr, nullptr,
                     &errdoc_response_handler, &er,
                     &request2->async_ref);
    } else
        errdoc_resubmit(er);
}

static void
errdoc_translate_error(GError *error, void *ctx)
{
    error_response &er = *(error_response *)ctx;

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

static void
errdoc_abort(struct async_operation *ao)
{
    error_response &er = (error_response &)ao;

    if (er.body != nullptr)
        istream_close_unused(er.body);

    er.async_ref.Abort();
}

static const struct async_operation_class errdoc_operation = {
    .abort = errdoc_abort,
};

/*
 * constructor
 *
 */

void
errdoc_dispatch_response(struct request &request2, http_status_t status,
                         ConstBuffer<void> error_document,
                         HttpHeaders &&headers, struct istream *body)
{
    assert(!error_document.IsNull());

    struct instance *instance = request2.connection->instance;

    assert(instance->translate_cache != nullptr);

    struct pool &pool = *request2.request->pool;
    error_response *er = NewFromPool<error_response>(pool);
    er->request2 = &request2;
    er->status = status;
    er->headers = std::move(headers);
    er->body = body != nullptr
        ? istream_hold_new(&pool, body)
        : nullptr;

    er->operation.Init(errdoc_operation);
    request2.async_ref.Set(er->operation);

    fill_translate_request(&er->translate_request,
                           &request2.translate.request,
                           error_document, status);
    translate_cache(pool, *instance->translate_cache,
                    er->translate_request,
                    errdoc_translate_handler, er,
                    er->async_ref);
}
