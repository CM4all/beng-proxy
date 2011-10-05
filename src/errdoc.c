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
#include "http-response.h"
#include "tcache.h"
#include "get.h"
#include "http-response.h"

#include <daemon/log.h>

struct error_response {
    struct async_operation operation;
    struct async_operation_ref async_ref;

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

/*
 * HTTP response handler
 *
 */

static void
errdoc_response_response(http_status_t status, struct strmap *headers,
                          istream_t body, void *ctx)
{
    struct error_response *er = ctx;

    if (http_status_is_success(status)) {
        if (er->body != NULL)
            /* close the original (error) response body */
            istream_close_unused(er->body);

        http_response_handler_direct_response(&response_handler, er->request2,
                                              er->status, headers, body);
    } else {
        if (body != NULL)
            /* discard the error document response */
            istream_close_unused(body);

        errdoc_resubmit(er);
    }
}

static void
errdoc_response_abort(GError *error, void *ctx)
{
    struct error_response *er = ctx;

    daemon_log(2, "error on error document of %s: %s\n",
               er->request2->request->uri, error->message);
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
errdoc_translate_response(const struct translate_response *response, void *ctx)
{
    struct error_response *er = ctx;

    if ((response->status == (http_status_t)0 ||
         http_status_is_success(response->status)) &&
        response->address.type != RESOURCE_ADDRESS_NONE) {
        struct request *request2 = er->request2;
        struct pool *pool = request2->request->pool;
        struct instance *instance = request2->connection->instance;

        resource_get(instance->http_cache,
                     instance->tcp_balancer,
                     instance->fcgi_stock, instance->was_stock,
                     instance->delegate_stock,
                     pool, 0, HTTP_METHOD_GET,
                     &response->address, HTTP_STATUS_OK, NULL, NULL,
                     &errdoc_response_handler, er,
                     &request2->async_ref);
    } else
        errdoc_resubmit(er);
}

static void
errdoc_translate_error(GError *error, void *ctx)
{
    struct error_response *er = ctx;

    daemon_log(2, "error document translation error: %s\n", error->message);
    g_error_free(error);

    errdoc_resubmit(er);
}

static const struct translate_handler errdoc_translate_handler = {
    .response = errdoc_translate_response,
    .error = errdoc_translate_error,
};

static void
fill_translate_request(struct translate_request *t,
                       const struct translate_request *src,
                       http_status_t status)
{
    *t = *src;
    t->error_document_status = status;
}

/*
 * async operation
 *
 */

static void
errdoc_abort(struct async_operation *ao)
{
    struct error_response *er = (struct error_response *)ao;

    if (er->body != NULL)
        istream_close_unused(er->body);

    async_abort(&er->async_ref);
}

static const struct async_operation_class errdoc_operation = {
    .abort = errdoc_abort,
};

/*
 * constructor
 *
 */

void
errdoc_dispatch_response(struct request *request2, http_status_t status,
                         struct growing_buffer *headers, istream_t body)
{
    struct instance *instance = request2->connection->instance;

    assert(instance->translate_cache != NULL);

    struct pool *pool = request2->request->pool;
    struct error_response *er = p_malloc(pool, sizeof(*er));
    er->request2 = request2;
    er->status = status;
    er->headers = headers;
    er->body = body != NULL
        ? istream_hold_new(pool, body)
        : NULL;

    async_init(&er->operation, &errdoc_operation);
    async_ref_set(&request2->async_ref, &er->operation);

    fill_translate_request(&er->translate_request,
                           &request2->translate.request, status);
    translate_cache(pool, instance->translate_cache,
                    &er->translate_request,
                    &errdoc_translate_handler, er,
                    &er->async_ref);
}
