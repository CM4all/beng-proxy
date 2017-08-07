/*
 * Implementation of a ACME client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "GlueHttpClient.hxx"
#include "event/Loop.hxx"
#include "curl/Request.hxx"
#include "curl/Handler.hxx"
#include "curl/Slist.hxx"
#include "util/ConstBuffer.hxx"

#include <exception>

GlueHttpClient::GlueHttpClient(EventLoop &event_loop)
    :curl_global(event_loop)
{
}

GlueHttpClient::~GlueHttpClient()
{
}

class GlueHttpResponseHandler final : public CurlResponseHandler {
    http_status_t status;
    std::multimap<std::string, std::string> headers;

    std::string body_string;

    std::exception_ptr error;

    bool done = false;

public:
    bool IsDone() const {
        return done;
    }

    void CheckThrowError() {
        if (error)
            std::rethrow_exception(error);
    }

    GlueHttpResponse MoveResponse() {
        return {status, std::move(headers), std::move(body_string)};
    }

public:
    /* virtual methods from class CurlResponseHandler */

    void OnHeaders(unsigned _status,
                   std::multimap<std::string, std::string> &&_headers) override {
        status = http_status_t(_status);
        headers = std::move(_headers);
    }

    void OnData(ConstBuffer<void> data) override {
        body_string.append((const char *)data.data, data.size);
    }

    void OnEnd() override {
        done = true;
    }

    void OnError(std::exception_ptr e) override {
        error = std::move(e);
        done = true;
    }
};

GlueHttpResponse
GlueHttpClient::Request(EventLoop &event_loop,
                        http_method_t method, const char *uri,
                        ConstBuffer<void> body)
{
    CurlSlist header_list;

    CurlEasy easy(uri);

    if (method == HTTP_METHOD_HEAD)
        easy.SetNoBody();
    else if (method == HTTP_METHOD_POST)
        easy.SetPost();

    if (!body.IsNull()) {
        easy.SetRequestBody(body.data, body.size);
        header_list.Append("Content-Type: application/json");
    }

    easy.SetRequestHeaders(header_list.Get());

    GlueHttpResponseHandler handler;
    CurlRequest request(curl_global, std::move(easy), handler);

    request.Start();

    while (!handler.IsDone() && event_loop.LoopOnce()) {}

    handler.CheckThrowError();
    return handler.MoveResponse();
}
