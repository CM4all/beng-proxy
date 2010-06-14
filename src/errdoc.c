/*
 * Error document handler.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "errdoc.h"
#include "request.h"
#include "connection.h"
#include "instance.h"
#include "http-server.h"
#include "tcache.h"
#include "get.h"

struct error_response {
    struct request *request2;

    http_status_t status;
    struct growing_buffer *headers;
    istream_t body;

    struct translate_request translate_request;
};

static void
errdoc_resubmit(const struct error_response *er)
{
    response_dispatch(er->request2, er->status, er->headers, er->body);
}

static void
errdoc_response_response(http_status_t status, struct strmap *headers,
                         istream_t body, void *ctx)
{
    struct error_response *er = ctx;

    if (http_status_is_success(status)) {
        if (er->body != NULL)
            /* close the original (error) response body */
            istream_close(er->body);

        http_response_handler_direct_response(&response_handler, er->request2,
                                              er->status, headers, body);
    } else {
        if (body != NULL)
            /* discard the error document response */
            istream_close(body);

        errdoc_resubmit(er);
    }
}

static void
errdoc_response_abort(void *ctx)
{
    struct error_response *er = ctx;

    errdoc_resubmit(er);
}

const struct http_response_handler errdoc_response_handler = {
    .response = errdoc_response_response,
    .abort = errdoc_response_abort,
};

static void
errdoc_translate_callback(const struct translate_response *response, void *ctx)
{
    struct error_response *er = ctx;

    if ((response->status == (http_status_t)0 ||
         http_status_is_success(response->status)) &&
        response->address.type != RESOURCE_ADDRESS_NONE) {
        struct request *request2 = er->request2;
        pool_t pool = request2->request->pool;
        struct instance *instance = request2->connection->instance;

        resource_get(instance->http_cache, instance->tcp_stock,
                     instance->fcgi_stock, instance->delegate_stock,
                     pool, HTTP_METHOD_GET,
                     &response->address, HTTP_STATUS_OK, NULL, NULL,
                     &errdoc_response_handler, er,
                     request2->async_ref);
    } else
        errdoc_resubmit(er);
}

static void
fill_translate_request(struct translate_request *t,
                       const struct translate_request *src,
                       http_status_t status)
{
    *t = *src;
    t->error_document_status = status;
}

void
errdoc_dispatch_response(struct request *request2, http_status_t status,
                         struct growing_buffer *headers, istream_t body)
{
    struct instance *instance = request2->connection->instance;

    assert(instance->translate_cache != NULL);

    pool_t pool = request2->request->pool;
    struct error_response *er = p_malloc(pool, sizeof(*er));
    er->request2 = request2;
    er->status = status;
    er->headers = headers;
    er->body = body != NULL
        ? istream_hold_new(pool, body)
        : NULL;

    fill_translate_request(&er->translate_request,
                           &request2->translate.request, status);
    translate_cache(pool, instance->translate_cache,
                    &er->translate_request,
                    errdoc_translate_callback, er,
                    request2->async_ref);
}
