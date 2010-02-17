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
    void (*response)(http_status_t status, struct strmap *headers,
                     istream_t body, void *ctx);
    void (*abort)(void *ctx);
};

struct http_response_handler_ref {
    const struct http_response_handler *handler;
    void *ctx;
};

static inline int
http_response_handler_defined(const struct http_response_handler_ref *ref)
{
    assert(ref != NULL);

    return ref->handler != NULL;
}

static inline void
http_response_handler_clear(struct http_response_handler_ref *ref)
{
    assert(ref != NULL);

    ref->handler = NULL;
}

static inline void
http_response_handler_set(struct http_response_handler_ref *ref,
                          const struct http_response_handler *handler,
                          void *ctx)
{
    assert(ref != NULL);
    assert(handler != NULL);
    assert(handler->response != NULL);
    assert(handler->abort != NULL);

    ref->handler = handler;
    ref->ctx = ctx;
}

static inline void
http_response_handler_direct_response(const struct http_response_handler *handler,
                                      void *ctx,
                                      http_status_t status,
                                      struct strmap *headers, istream_t body)
{
    assert(handler != NULL);
    assert(handler->response != NULL);
    assert(http_status_is_valid(status));
    assert(!http_status_is_empty(status) || body == NULL);

    handler->response(status, headers, body, ctx);
}

static inline void
http_response_handler_direct_abort(const struct http_response_handler *handler,
                                   void *ctx)
{
    assert(handler != NULL);
    assert(handler->abort != NULL);

    handler->abort(ctx);
}

/**
 * Sends a plain-text message.
 */
static inline void
http_response_handler_direct_message(const struct http_response_handler *handler,
                                     void *ctx,
                                     pool_t pool,
                                     http_status_t status, const char *msg)
{
    struct strmap *headers = strmap_new(pool, 2);
    strmap_add(headers, "content-type", "text/plain; charset=utf-8");
    http_response_handler_direct_response(handler, ctx, status, headers,
                                          istream_string_new(pool, msg));
}

static inline void
http_response_handler_invoke_response(struct http_response_handler_ref *ref,
                                      http_status_t status,
                                      struct strmap *headers, istream_t body)
{
    const struct http_response_handler *handler;

    assert(ref != NULL);
    assert(ref->handler != NULL);
    assert(ref->handler->response != NULL);
    assert(http_status_is_valid(status));
    assert(!http_status_is_empty(status) || body == NULL);

    handler = ref->handler;
#ifndef NDEBUG
    http_response_handler_clear(ref);
#endif

    handler->response(status, headers, body, ref->ctx);
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
    http_response_handler_clear(ref);
#endif

    handler->abort(ref->ctx);
}

/**
 * Sends a plain-text message.
 */
static inline void
http_response_handler_invoke_message(struct http_response_handler_ref *ref,
                                     pool_t pool,
                                     http_status_t status, const char *msg)
{
    struct strmap *headers = strmap_new(pool, 2);
    strmap_add(headers, "content-type", "text/plain; charset=utf-8");
    http_response_handler_invoke_response(ref, status, headers,
                                          istream_string_new(pool, msg));
}

#endif
