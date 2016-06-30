/*
 * Definition of the HTTP response handler.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_RESPONSE_HXX
#define BENG_PROXY_HTTP_RESPONSE_HXX

#include "glibfwd.hxx"

#include <http/status.h>

#include <utility>

#include <assert.h>

struct pool;
class StringMap;
class Istream;

class HttpResponseHandler {
protected:
    virtual void OnHttpResponse(http_status_t status, StringMap &&headers,
                                Istream *body) = 0;

    virtual void OnHttpError(GError *error) = 0;

public:
    void InvokeResponse(http_status_t status, StringMap &&headers,
                        Istream *body) {
        assert(http_status_is_valid(status));
        assert(!http_status_is_empty(status) || body == nullptr);

        OnHttpResponse(status, std::move(headers), body);
    }

    /**
     * Sends a plain-text message.
     */
    void InvokeResponse(struct pool &pool,
                        http_status_t status, const char *msg);

    void InvokeError(GError *error) {
        assert(error != nullptr);

        OnHttpError(error);
    }
};

struct http_response_handler_ref {
    HttpResponseHandler *handler;

#ifndef NDEBUG
    bool used = false;

    bool IsUsed() const {
        return used;
    }
#endif

    http_response_handler_ref() = default;

    explicit constexpr http_response_handler_ref(HttpResponseHandler &_handler)
        :handler(&_handler) {}

    bool IsDefined() const {
        return handler != nullptr;
    }

    void Clear() {
        handler = nullptr;
    }

    void Set(HttpResponseHandler &_handler) {
        handler = &_handler;

#ifndef NDEBUG
        used = false;
#endif
    }

    void InvokeResponse(http_status_t status, StringMap &&headers,
                        Istream *body) {
        assert(handler != nullptr);
        assert(!IsUsed());

#ifndef NDEBUG
        used = true;
#endif

        handler->InvokeResponse(status, std::move(headers), body);
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

        handler->InvokeResponse(pool, status, msg);
    }

    void InvokeError(GError *error) {
        assert(handler != nullptr);
        assert(!IsUsed());

#ifndef NDEBUG
        used = true;
#endif

        handler->InvokeError(error);
    }
};

#endif
