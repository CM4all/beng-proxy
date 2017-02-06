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

/**
 * A helper class which feeds a (foreign) memory buffer into the
 * CURLOPT_READFUNCTION.
 */
class CurlRequestBody {
    ConstBuffer<char> data;

public:
    explicit CurlRequestBody(ConstBuffer<void> _data)
        :data(ConstBuffer<char>::FromVoid(_data)) {}

    void Enable(CurlRequest &request) {
        request.SetOption(CURLOPT_READFUNCTION, Callback);
        request.SetOption(CURLOPT_READDATA, this);
    }

private:
    size_t Read(char *buffer, size_t size) {
        size_t n = std::min(size, data.size);
        std::copy_n(data.begin(), n, buffer);
        return n;
    }

    static size_t Callback(char *buffer, size_t size, size_t nitems,
                           void *instream) {
        auto &rb = *(CurlRequestBody *)instream;
        return rb.Read(buffer, size * nitems);
    }
};

GlueHttpResponse
GlueHttpClient::Request(EventLoop &event_loop,
                        GlueHttpServerAddress &server,
                        http_method_t method, const char *uri,
                        ConstBuffer<void> _body)
{
    std::string url = server.url + uri;

    GlueHttpResponseHandler handler;
    CurlRequest request(curl_global, url.c_str(), handler);

    if (method == HTTP_METHOD_HEAD)
        request.SetOption(CURLOPT_NOBODY, 1l);

    CurlRequestBody body(_body);
    if (!_body.IsNull())
        body.Enable(request);

    request.Start();

    while (!handler.IsDone() && event_loop.LoopOnce()) {}

    handler.CheckThrowError();
    return handler.MoveResponse();
}
