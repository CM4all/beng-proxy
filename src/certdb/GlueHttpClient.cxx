/*
 * Implementation of a ACME client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "GlueHttpClient.hxx"
#include "event/Loop.hxx"
#include "curl/Request.hxx"
#include "curl/Handler.hxx"
#include "util/ConstBuffer.hxx"

#include <exception>

GlueHttpServerAddress::GlueHttpServerAddress(bool ssl,
                                             const char *_host_and_port)
    :url(ssl ? "https://" : "http://")
{
    url += _host_and_port;
}

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
                        GlueHttpServerAddress &server,
                        http_method_t method, const char *uri,
                        ConstBuffer<void> body)
{
    std::string url = server.url + uri;

    GlueHttpResponseHandler handler;
    CurlRequest request(curl_global, url.c_str(), handler);

    if (method == HTTP_METHOD_HEAD)
        request.SetOption(CURLOPT_NOBODY, 1l);
    else if (method == HTTP_METHOD_POST)
        request.SetOption(CURLOPT_POST, 1l);

    if (!body.IsNull()) {
        request.SetOption(CURLOPT_POSTFIELDS, (const char *)body.data);
        request.SetOption(CURLOPT_POSTFIELDSIZE, long(body.size));
    }

    request.Start();

    while (!handler.IsDone() && event_loop.LoopOnce()) {}

    handler.CheckThrowError();
    return handler.MoveResponse();
}
