/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "HttpConnection.hxx"
#include "GotoConfig.hxx"
#include "ListenerConfig.hxx"
#include "lb_instance.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_response.hxx"
#include "http_headers.hxx"
#include "GException.hxx"

class LbLuaResponseHandler final : public HttpResponseHandler {
    LbHttpConnection &connection;

    HttpServerRequest &request;

    bool finished = false;

public:
    LbLuaResponseHandler(LbHttpConnection &_connection,
                         HttpServerRequest &_request)
        :connection(_connection), request(_request) {}

    bool IsFinished() const {
        return finished;
    }

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(GError *error) override;
};

void
LbLuaResponseHandler::OnHttpResponse(http_status_t status,
                                     StringMap &&_headers,
                                     Istream *response_body)
{
    finished = true;

    HttpHeaders headers(std::move(_headers));

    if (request.method == HTTP_METHOD_HEAD)
        /* pass Content-Length, even though there is no response body
           (RFC 2616 14.13) */
        headers.MoveToBuffer("content-length");

    http_server_response(&request, status, std::move(headers), response_body);
}

void
LbLuaResponseHandler::OnHttpError(GError *error)
{
    finished = true;

    connection.LogSendError(request, error);

    g_error_free(error);
}

void
LbHttpConnection::InvokeLua(const LbLuaHandlerConfig &config,
                            HttpServerRequest &request,
                            CancellablePointer &cancel_ptr)
{
    auto *handler = instance.goto_map.FindLuaHandler(config.name.c_str());
    assert(handler != nullptr);

    LbLuaResponseHandler response_handler(*this, request);
    const LbGotoConfig *g;

    try {
        g = handler->HandleRequest(request, response_handler);
    } catch (const std::runtime_error &e) {
        if (response_handler.IsFinished())
            daemon_log(1, "Lua error: %s\n", e.what());
        else
            response_handler.InvokeError(ToGError(e));
        return;
    }

    if (response_handler.IsFinished())
        return;

    if (g == nullptr) {
        http_server_send_message(&request, HTTP_STATUS_BAD_GATEWAY,
                                 "No response from Lua handler");
        return;
    }

    HandleHttpRequest(*g, request, cancel_ptr);
    return;
}
