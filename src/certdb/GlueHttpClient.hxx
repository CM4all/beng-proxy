/*
 * Implementation of a ACME client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_GLUE_HTTP_CLIENT_HXX
#define BENG_PROXY_GLUE_HTTP_CLIENT_HXX

#include "curl/Global.hxx"

#include <http/method.h>
#include <http/status.h>

#include <string>
#include <map>

template<typename T> struct ConstBuffer;
class EventLoop;

struct GlueHttpResponse {
    http_status_t status;

    std::multimap<std::string, std::string> headers;

    std::string body;

    GlueHttpResponse(http_status_t _status,
                     std::multimap<std::string, std::string> &&_headers,
                     std::string &&_body)
        :status(_status), headers(std::move(_headers)), body(_body) {}
};

class GlueHttpClient {
    CurlGlobal curl_global;

public:
    explicit GlueHttpClient(EventLoop &event_loop);
    ~GlueHttpClient();

    GlueHttpClient(const GlueHttpClient &) = delete;
    GlueHttpClient &operator=(const GlueHttpClient &) = delete;

    GlueHttpResponse Request(EventLoop &event_loop,
                             http_method_t method, const char *uri,
                             ConstBuffer<void> body);
};

#endif
