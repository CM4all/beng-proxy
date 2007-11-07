/*
 * Definition of the HTTP response handler.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_RESPONSE_H
#define __BENG_HTTP_RESPONSE_H

#include "strmap.h"
#include "istream.h"
#include "http.h"

#include <assert.h>
#include <stddef.h>

struct http_response_handler {
    void (*response)(http_status_t status, strmap_t headers,
                     off_t content_length, istream_t body,
                     void *ctx);
    void (*abort)(void *ctx);
};

struct http_response_handler_ref {
    const struct http_response_handler *handler;
    void *ctx;
};

static inline void
http_response_handler_set(struct http_response_handler_ref *ref,
                          const struct http_response_handler *handler,
                          void *ctx)
{
    assert(ref != NULL);
    assert(handler != NULL);
    assert(handler->response != NULL);

    ref->handler = handler;
    ref->ctx = ctx;
}

static inline void
http_response_handler_invoke_response(struct http_response_handler_ref *ref,
                                      http_status_t status, strmap_t headers,
                                      off_t content_length, istream_t body)
{
    const struct http_response_handler *handler;

    assert(ref != NULL);
    assert(ref->handler != NULL);
    assert(ref->handler->response != NULL);

    handler = ref->handler;
#ifndef NDEBUG
    ref->handler = NULL;
#endif

    handler->response(status, headers, content_length, body,
                      ref->ctx);
}

static inline void
http_response_handler_invoke_abort(struct http_response_handler_ref *ref)
{
    const struct http_response_handler *handler;

    assert(ref != NULL);
    assert(ref->handler != NULL);
    assert(ref->handler->abort != NULL);

    handler = ref->handler;
#ifndef NDEBUG
    ref->handler = NULL;
#endif

    handler->abort(ref->ctx);
}

#ifndef NDEBUG
static inline int
http_response_handler_cleared(const struct http_response_handler_ref *ref)
{
    return ref->handler == NULL;
}
#endif

#endif
