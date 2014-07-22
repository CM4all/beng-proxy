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

    void InvokeResponse(void *ctx,
                        http_status_t status, struct strmap *headers,
                        struct istream *body) const {
        assert(response != nullptr);
        assert(abort != nullptr);
        assert(http_status_is_valid(status));
        assert(!http_status_is_empty(status) || body == nullptr);

        response(status, headers, body, ctx);
    }

    /**
     * Sends a plain-text message.
     */
    void InvokeMessage(void *ctx, struct pool &pool,
                       http_status_t status, const char *msg) const;

    void InvokeAbort(void *ctx, GError *error) const {
        assert(response != nullptr);
        assert(abort != nullptr);
        assert(error != nullptr);

        abort(error, ctx);
    }
};

struct http_response_handler_ref {
    const struct http_response_handler *handler;
    void *ctx;

#ifndef NDEBUG
    bool used;

    bool IsUsed() const {
        return used;
    }
#endif

    bool IsDefined() const {
        return handler != nullptr;
    }

    void Clear() {
        handler = nullptr;
    }

    void Set(const struct http_response_handler &_handler, void *_ctx) {
        assert(_handler.response != nullptr);
        assert(_handler.abort != nullptr);

        handler = &_handler;
        ctx = _ctx;

#ifndef NDEBUG
        used = false;
#endif
    }

    void InvokeResponse(http_status_t status, struct strmap *headers,
                        struct istream *body) {
        assert(handler != nullptr);
        assert(!IsUsed());

#ifndef NDEBUG
        used = true;
#endif

        handler->InvokeResponse(ctx, status, headers, body);
    }

    /**
     * Sends a plain-text message.
     */
    void InvokeMessage(struct pool &pool,
                       http_status_t status, const char *msg) {
        assert(handler != nullptr);
        assert(!IsUsed());

#ifndef NDEBUG
        used = true;
#endif

        handler->InvokeMessage(ctx, pool, status, msg);
    }

    void InvokeAbort(GError *error) {
        assert(handler != nullptr);
        assert(!IsUsed());

#ifndef NDEBUG
        used = true;
#endif

        handler->InvokeAbort(ctx, error);
    }
};

#endif
