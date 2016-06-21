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
#include "event/Loop.hxx"
#include "async.hxx"
#include "istream/Handler.hxx"
#include "istream/Pointer.hxx"
#include "fb_pool.hxx"
#include "thread_pool.hxx"
#include "util/ScopeExit.hxx"

#include <stdexcept>

#include <string.h>
#include <netdb.h>

gcc_noreturn
static void
ThrowError(GError *error)
{
    AtScopeExit(error) { g_error_free(error); };
    throw std::runtime_error(error->message);
}

static void
CheckThrowError(GError *error)
{
    if (error != nullptr)
        ThrowError(error);
}

static void
ResolveOrThrow(struct pool &p, AddressList &address_list,
               const char *host_and_port, int default_port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    GError *error = nullptr;
    if (!address_list_resolve(&p, &address_list,
                              host_and_port, default_port,
                              &hints, &error))
        ThrowError(error);
}

GlueHttpServerAddress::GlueHttpServerAddress(struct pool &p, bool _ssl,
                                             const char *_host_and_port,
                                             int default_port)
    :host_and_port(_host_and_port),
     ssl(_ssl)
{
    ResolveOrThrow(p, addresses, _host_and_port, default_port);
}

GlueHttpClient::GlueHttpClient(struct pool &p, EventLoop &event_loop)
    :balancer(balancer_new(p, event_loop)),
     tcp_stock(tcp_stock_new(event_loop, 0)),
     tcp_balancer(tcp_balancer_new(*tcp_stock, *balancer))
{
    fb_pool_init(event_loop, false);
    ssl_client_init();
}

GlueHttpClient::~GlueHttpClient()
{
    thread_pool_stop();

    ssl_client_deinit();
    fb_pool_deinit();
    tcp_balancer_free(tcp_balancer);
    delete tcp_stock;
    balancer_free(balancer);

    thread_pool_join();
    thread_pool_deinit();
}

class SslSocketFilterFactory final : public SocketFilterFactory {
    struct pool &pool;
    EventLoop &event_loop;
    const char *const host;

public:
    SslSocketFilterFactory(struct pool &_pool,
                           EventLoop &_event_loop,
                           const char *_host)
        :pool(_pool), event_loop(_event_loop), host(_host) {}

    void *CreateFilter(GError **error_r) override {
        return ssl_client_create(&pool, event_loop, host, error_r);
    }
};

void
GlueHttpClient::Request(struct pool &p, EventLoop &event_loop,
                        GlueHttpServerAddress &server,
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
        filter_factory = NewFromPool<SslSocketFilterFactory>(p, p, event_loop,
                                                             /* TODO: only host */
                                                             server.host_and_port);
    }

    auto *address = NewFromPool<HttpAddress>(p, ShallowCopy(),
                                             HttpAddress::Protocol::HTTP,
                                             server.ssl,
                                             server.host_and_port, uri,
                                             server.addresses);

    http_request(p, event_loop, *tcp_balancer, 0,
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
GlueHttpClient::Request(EventLoop &event_loop,
                        struct pool &p, GlueHttpServerAddress &server,
                        http_method_t method, const char *uri,
                        HttpHeaders &&headers, Istream *body)
{
    struct async_operation_ref async_ref;

    GlueHttpRequest request;
    Request(p, event_loop, server, method, uri, std::move(headers), body,
            GlueHttpRequest::handler, &request, async_ref);
    while (!request.IsDone() && event_loop.LoopOnce()) {}

    request.CheckThrowError();

    return request.MoveResponse();
}
