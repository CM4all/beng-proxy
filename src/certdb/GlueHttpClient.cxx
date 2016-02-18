/*
 * Implementation of a ACME client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "GlueHttpClient.hxx"
#include "balancer.hxx"
#include "tcp_stock.hxx"
#include "stock/MapStock.hxx"
#include "tcp_balancer.hxx"
#include "address_resolver.hxx"
#include "http_request.hxx"
#include "http_response.hxx"
#include "http_address.hxx"
#include "pool.hxx"
#include "filtered_socket.hxx"
#include "ssl/ssl_client.hxx"
#include "event/Base.hxx"
#include "async.hxx"
#include "istream/Handler.hxx"
#include "istream/istream_pointer.hxx"
#include "fb_pool.hxx"
#include "thread_pool.hxx"

#include <stdexcept>

#include <string.h>
#include <netdb.h>

gcc_noreturn
static void
ThrowError(GError *error)
{
    std::string msg(error->message);
    g_error_free(error);
    throw std::runtime_error(std::move(msg));
}

static void
CheckThrowError(GError *error)
{
    if (error != nullptr)
        ThrowError(error);
}

static AddressList &
ResolveOrThrow(struct pool &p, const char *host_and_port, int default_port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    GError *error = nullptr;
    auto *al = address_list_resolve_new(&p, host_and_port, default_port,
                                        &hints, &error);
    if (al == nullptr)
        ThrowError(error);

    return *al;
}

GlueHttpServerAddress::GlueHttpServerAddress(struct pool &p, bool _ssl,
                                             const char *_host_and_port,
                                             int default_port)
    :host_and_port(_host_and_port),
     addresses(ResolveOrThrow(p, _host_and_port, default_port)),
     ssl(_ssl) {}

GlueHttpClient::GlueHttpClient(struct pool &p)
    :balancer(balancer_new(p)),
     tcp_stock(tcp_stock_new(0)),
     tcp_balancer(tcp_balancer_new(*tcp_stock, *balancer))
{
    fb_pool_init(false);
    ssl_client_init();
}

GlueHttpClient::~GlueHttpClient()
{
    thread_pool_stop();

    ssl_client_deinit();
    fb_pool_deinit();
    tcp_balancer_free(tcp_balancer);
    hstock_free(tcp_stock);
    balancer_free(balancer);

    thread_pool_join();
    thread_pool_deinit();
}

class SslSocketFilterFactory final : public SocketFilterFactory {
    struct pool &pool;
    const char *const host;

public:
    SslSocketFilterFactory(struct pool &_pool,
                           const char *_host)
        :pool(_pool), host(_host) {}

    void *CreateFilter(GError **error_r) override {
        return ssl_client_create(&pool, host, error_r);
    }
};

void
GlueHttpClient::Request(struct pool &p, GlueHttpServerAddress &server,
                        http_method_t method, const char *uri,
                        HttpHeaders &&headers, Istream *body,
                        const struct http_response_handler &handler,
                        void *handler_ctx,
                        struct async_operation_ref &async_ref)
{
    const SocketFilter *filter = nullptr;
    SocketFilterFactory *filter_factory = nullptr;

    if (server.ssl) {
        filter = &ssl_client_get_filter();
        filter_factory = NewFromPool<SslSocketFilterFactory>(p, p,
                                                             /* TODO: only host */
                                                             server.host_and_port);
    }

        auto *address = NewFromPool<HttpAddress>(p);
    address->Init(URI_SCHEME_HTTP, server.ssl,
                  server.host_and_port, uri);
    address->addresses.CopyFrom(&p, server.addresses);

    http_request(p, *tcp_balancer, 0,
                 filter, filter_factory,
                 method, *address,
                 std::move(headers), body,
                 handler, handler_ctx, async_ref);
}

class GlueHttpRequest final : IstreamHandler {
    http_status_t status;
    struct strmap *headers;
    IstreamPointer body;

    std::string body_string;

    GError *error = nullptr;

    bool done = false;

public:
    GlueHttpRequest():body(nullptr) {}

    ~GlueHttpRequest() {
        if (error != nullptr)
            g_error_free(error);
    }

    bool IsDone() const {
        return done;
    }

    void CheckThrowError() {
        GError *_error = error;
        error = nullptr;
        ::CheckThrowError(_error);
    }

    GlueHttpResponse MoveResponse() {
        return {status, *headers, std::move(body_string)};
    }

private:
    /* virtual methods from class IstreamHandler */

    size_t OnData(const void *data, size_t length) override {
        body_string.append((const char *)data, length);
        return length;
    }

    void OnEof() override {
        done = true;
    }

    void OnError(GError *_error) override {
        error = _error;
        done = true;
    }

    /* virtual methods from struct http_response_handler */

    void OnResponse(http_status_t _status, struct strmap *_headers,
                    Istream *_body) {
        assert(error == nullptr);

        status = _status;
        headers = _headers;

        if (_body != nullptr) {
            body.Set(*_body, *this);
            body.Read();
        } else
            done = true;
    }

    void OnResponseError(GError *_error) {
        assert(error == nullptr);

        error = _error;
        done = true;
    }

    static void OnResponse(http_status_t status, struct strmap *headers,
                           Istream *body, void *ctx) {
        ((GlueHttpRequest *)ctx)->OnResponse(status, headers, body);
    }

    static void OnResponseError(GError *_error, void *ctx) {
        ((GlueHttpRequest *)ctx)->OnResponseError(_error);
    }

public:
    static const struct http_response_handler handler;
};

constexpr struct http_response_handler GlueHttpRequest::handler = {
    OnResponse,
    OnResponseError,
};

GlueHttpResponse
GlueHttpClient::Request(EventBase &event_base,
                        struct pool &p, GlueHttpServerAddress &server,
                        http_method_t method, const char *uri,
                        HttpHeaders &&headers, Istream *body)
{
    struct async_operation_ref async_ref;

    GlueHttpRequest request;
    Request(p, server, method, uri, std::move(headers), body,
            GlueHttpRequest::handler, &request, async_ref);
    while (!request.IsDone() && event_base.LoopOnce()) {}

    request.CheckThrowError();

    return request.MoveResponse();
}
