/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "HttpConnection.hxx"
#include "GotoConfig.hxx"
#include "lb_instance.hxx"
#include "lb_config.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "translation/Handler.hxx"
#include "translation/Response.hxx"
#include "GException.hxx"
#include "pool.hxx"

/*
 * TranslateHandler
 *
 */

struct LbHttpRequest {
    LbHttpConnection &connection;
    LbTranslationHandler &handler;
    HttpServerRequest &request;
    CancellablePointer &cancel_ptr;

    LbHttpRequest(LbHttpConnection &_connection,
                  LbTranslationHandler &_handler,
                  HttpServerRequest &_request,
                  CancellablePointer &_cancel_ptr)
        :connection(_connection), handler(_handler),
         request(_request), cancel_ptr(_cancel_ptr) {}
};

static void
lb_http_translate_response(TranslateResponse &response, void *ctx)
{
    auto &r = *(LbHttpRequest *)ctx;
    auto &c = r.connection;
    auto &request = r.request;

    if (response.status != http_status_t(0) || response.redirect != nullptr ||
        response.message != nullptr) {
        auto status = response.status;
        if (status == http_status_t(0))
            status = HTTP_STATUS_SEE_OTHER;

        const char *body = response.message;
        if (body == nullptr)
            body = http_status_to_string(status);

        http_server_simple_response(request, status,
                                    response.redirect,
                                    body);
    } else if (response.pool != nullptr) {
        auto *destination = r.handler.FindDestination(response.pool);
        if (destination == nullptr) {
            c.LogSendError(request,
                           ToGError(std::runtime_error("No such pool")));
            return;
        }

        if (response.canonical_host != nullptr)
            c.canonical_host = response.canonical_host;

        c.HandleHttpRequest(*destination, request, r.cancel_ptr);
    } else {
        c.LogSendError(request,
                       ToGError(std::runtime_error("Invalid translation server response")));
    }
}

static void
lb_http_translate_error(std::exception_ptr ep, void *ctx)
{
    auto &r = *(LbHttpRequest *)ctx;

    r.connection.LogSendError(r.request, ToGError(ep));
}

static constexpr TranslateHandler lb_http_translate_handler = {
    .response = lb_http_translate_response,
    .error = lb_http_translate_error,
};

/*
 * constructor
 *
 */

void
LbHttpConnection::AskTranslationServer(LbTranslationHandler &handler,
                                       HttpServerRequest &request,
                                       CancellablePointer &cancel_ptr)
{
    auto *r = NewFromPool<LbHttpRequest>(request.pool, *this, handler, request,
                                         cancel_ptr);

    handler.Pick(request.pool, request,
                 lb_http_translate_handler, r,
                 cancel_ptr);
}
