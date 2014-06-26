/*
 * Definition of the HTTP response handler.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_RESPONSE_HXX
#define BENG_PROXY_HTTP_RESPONSE_HXX

#include "glibfwd.hxx"

#include <http/status.h>

#include <assert.h>

struct pool;
struct strmap;
struct istream;

struct http_response_handler {
    void (*response)(http_status_t status, struct strmap *headers,
                     struct istream *body, void *ctx);
    void (*abort)(GError *error, void *ctx);
};

struct http_response_handler_ref {
    const struct http_response_handler *handler;
    void *ctx;

#ifndef NDEBUG
    bool used;
#endif
};

#ifndef NDEBUG

static inline int
http_response_handler_used(const struct http_response_handler_ref *ref)
{
    assert(ref != nullptr);

    return ref->used;
}

#endif

static inline int
http_response_handler_defined(const struct http_response_handler_ref *ref)
{
    assert(ref != nullptr);

    return ref->handler != nullptr;
}

static inline void
http_response_handler_clear(struct http_response_handler_ref *ref)
{
    assert(ref != nullptr);

    ref->handler = nullptr;
}

static inline void
http_response_handler_set(struct http_response_handler_ref *ref,
                          const struct http_response_handler *handler,
                          void *ctx)
{
    assert(ref != nullptr);
    assert(handler != nullptr);
    assert(handler->response != nullptr);
    assert(handler->abort != nullptr);

    ref->handler = handler;
    ref->ctx = ctx;

#ifndef NDEBUG
    ref->used = false;
#endif
}

static inline void
http_response_handler_direct_response(const struct http_response_handler *handler,
                                      void *ctx,
                                      http_status_t status,
                                      struct strmap *headers,
                                      struct istream *body)
{
    assert(handler != nullptr);
    assert(handler->response != nullptr);
    assert(http_status_is_valid(status));
    assert(!http_status_is_empty(status) || body == nullptr);

    handler->response(status, headers, body, ctx);
}

static inline void
http_response_handler_direct_abort(const struct http_response_handler *handler,
                                   void *ctx,
                                   GError *error)
{
    assert(handler != nullptr);
    assert(handler->abort != nullptr);
    assert(error != nullptr);

    handler->abort(error, ctx);
}

/**
 * Sends a plain-text message.
 */
void
http_response_handler_direct_message(const struct http_response_handler *handler,
                                     void *ctx,
                                     struct pool *pool,
                                     http_status_t status, const char *msg);

static inline void
http_response_handler_invoke_response(struct http_response_handler_ref *ref,
                                      http_status_t status,
                                      struct strmap *headers,
                                      struct istream *body)
{
    const struct http_response_handler *handler;

    assert(ref != nullptr);
    assert(ref->handler != nullptr);
    assert(ref->handler->response != nullptr);
    assert(!http_response_handler_used(ref));
    assert(http_status_is_valid(status));
    assert(!http_status_is_empty(status) || body == nullptr);

    handler = ref->handler;
#ifndef NDEBUG
    ref->used = true;
#endif

    handler->response(status, headers, body, ref->ctx);
}

static inline void
http_response_handler_invoke_abort(struct http_response_handler_ref *ref,
                                   GError *error)
{
    const struct http_response_handler *handler;

    assert(ref != nullptr);
    assert(ref->handler != nullptr);
    assert(ref->handler->abort != nullptr);
    assert(error != nullptr);
    assert(!http_response_handler_used(ref));

    handler = ref->handler;
#ifndef NDEBUG
    ref->used = true;
#endif

    handler->abort(error, ref->ctx);
}

/**
 * Sends a plain-text message.
 */
void
http_response_handler_invoke_message(struct http_response_handler_ref *ref,
                                     struct pool *pool,
                                     http_status_t status, const char *msg);

#endif
