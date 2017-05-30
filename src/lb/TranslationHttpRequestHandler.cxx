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
    HttpServerRequest &request;
    CancellablePointer &cancel_ptr;

    LbHttpRequest(LbHttpConnection &_connection,
                  HttpServerRequest &_request,
                  CancellablePointer &_cancel_ptr)
        :connection(_connection), request(_request), cancel_ptr(_cancel_ptr) {}
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
        const auto *cluster = c.instance.config->FindCluster(response.pool);
        if (cluster == nullptr) {
            c.LogSendError(request,
                           ToGError(std::runtime_error("No such pool")));
            return;
        }

        c.ForwardHttpRequest(*cluster, request, r.cancel_ptr);
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
LbHttpConnection::AskTranslationServer(const LbTranslationHandlerConfig &destination,
                                       HttpServerRequest &request,
                                       CancellablePointer &cancel_ptr)
{
    auto *h = instance.translation_handlers.Find(destination.name);
    assert(h != nullptr);

    auto *r = NewFromPool<LbHttpRequest>(request.pool, *this, request,
                                         cancel_ptr);

    h->Pick(request.pool, request,
            lb_http_translate_handler, r,
            cancel_ptr);
}
