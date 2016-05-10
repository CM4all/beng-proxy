/*
 * Implementation of a ACME client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_GLUE_HTTP_CLIENT_HXX
#define BENG_PROXY_GLUE_HTTP_CLIENT_HXX

#include "http_headers.hxx"

#include <http/method.h>
#include <http/status.h>

#include <string>

struct pool;
struct balancer;
struct StockMap;
struct TcpBalancer;
struct AddressList;
struct SocketFilter;
class SocketFilterFactory;
class Istream;
class EventLoop;
class HttpHeaders;

struct GlueHttpServerAddress {
    const char *const host_and_port;

    const AddressList &addresses;

    const bool ssl;

    GlueHttpServerAddress(struct pool &p, bool _ssl,
                          const char *_host_and_port, int default_port);
};

struct GlueHttpResponse {
    http_status_t status;

    struct strmap &headers;

    std::string body;
};

class GlueHttpClient {
    struct balancer *const balancer;
    StockMap *const tcp_stock;
    TcpBalancer *const tcp_balancer;

public:
    GlueHttpClient(struct pool &p);
    ~GlueHttpClient();

    void Request(struct pool &p, EventLoop &event_loop,
                 GlueHttpServerAddress &server,
                 http_method_t method, const char *uri,
                 HttpHeaders &&headers, Istream *body,
                 const struct http_response_handler &handler,
                 void *handler_ctx,
                 struct async_operation_ref &async_ref);

    GlueHttpResponse Request(EventLoop &event_loop,
                             struct pool &p, GlueHttpServerAddress &server,
                             http_method_t method, const char *uri,
                             HttpHeaders &&headers=HttpHeaders(),
                             Istream *body=nullptr);
};

#endif
