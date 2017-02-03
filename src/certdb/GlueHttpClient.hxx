/*
 * Implementation of a ACME client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_GLUE_HTTP_CLIENT_HXX
#define BENG_PROXY_GLUE_HTTP_CLIENT_HXX

#include "http_headers.hxx"
#include "net/AddressInfo.hxx"
#include "strmap.hxx"

#include <http/method.h>
#include <http/status.h>

#include <string>

template<typename T> struct ConstBuffer;
struct pool;
struct Balancer;
class StockMap;
struct TcpBalancer;
class EventLoop;
class HttpHeaders;
class HttpResponseHandler;
class CancellablePointer;

struct GlueHttpServerAddress {
    const char *const host_and_port;

    AddressInfo addresses;

    const bool ssl;

    GlueHttpServerAddress(bool _ssl,
                          const char *_host_and_port, int default_port);
};

struct GlueHttpResponse {
    http_status_t status;

    StringMap headers;

    std::string body;

    GlueHttpResponse(http_status_t _status,
                     StringMap &&_headers, std::string &&_body)
        :status(_status), headers(std::move(_headers)), body(_body) {}
};

class GlueHttpClient {
    Balancer *const balancer;
    StockMap *const tcp_stock;
    TcpBalancer *const tcp_balancer;

public:
    explicit GlueHttpClient(EventLoop &event_loop);
    ~GlueHttpClient();

    GlueHttpClient(const GlueHttpClient &) = delete;
    GlueHttpClient &operator=(const GlueHttpClient &) = delete;

private:
    void Request(struct pool &p, EventLoop &event_loop,
                 GlueHttpServerAddress &server,
                 http_method_t method, const char *uri,
                 ConstBuffer<void> body,
                 HttpResponseHandler &handler,
                 CancellablePointer &cancel_ptr);

public:
    GlueHttpResponse Request(EventLoop &event_loop,
                             struct pool &p, GlueHttpServerAddress &server,
                             http_method_t method, const char *uri,
                             ConstBuffer<void> body);
};

#endif
