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
#include "http_request.hxx"
#include "http_response.hxx"
#include "http_address.hxx"
#include "pool.hxx"
#include "filtered_socket.hxx"
#include "ssl/ssl_client.hxx"
#include "event/Loop.hxx"
#include "istream/Handler.hxx"
#include "istream/Pointer.hxx"
#include "istream/istream_memory.hxx"
#include "fb_pool.hxx"
#include "thread_pool.hxx"
#include "net/Resolver.hxx"
#include "util/ScopeExit.hxx"
#include "util/ConstBuffer.hxx"

#include <glib.h>

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

static AddressInfo
ResolveOrThrow(const char *host_and_port, int default_port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_socktype = SOCK_STREAM;

    return Resolve(host_and_port, default_port, &hints);
}

GlueHttpServerAddress::GlueHttpServerAddress(bool _ssl,
                                             const char *_host_and_port,
                                             int default_port)
    :host_and_port(_host_and_port),
     addresses(ResolveOrThrow(_host_and_port, default_port)),
     ssl(_ssl)
{
}

GlueHttpClient::GlueHttpClient(EventLoop &event_loop)
    :balancer(balancer_new(event_loop)),
     tcp_stock(tcp_stock_new(event_loop, 0)),
     tcp_balancer(tcp_balancer_new(*tcp_stock, *balancer))
{
    fb_pool_init();
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

    void *CreateFilter() override {
        return ssl_client_create(&pool, event_loop, host);
    }
};

void
GlueHttpClient::Request(struct pool &p, EventLoop &event_loop,
                        GlueHttpServerAddress &server,
                        http_method_t method, const char *uri,
                        ConstBuffer<void> _body,
                        HttpResponseHandler &handler,
                        CancellablePointer &cancel_ptr)
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
                                             AddressList(ShallowCopy(),
                                                         server.addresses));

    auto *body = _body.IsNull()
        ? nullptr
        : istream_memory_new(&p, _body.data, _body.size);

    http_request(p, event_loop, *tcp_balancer, 0,
                 filter, filter_factory,
                 method, *address,
                 HttpHeaders(p), body,
                 handler, cancel_ptr);
}

class GlueHttpRequest final : IstreamHandler, public HttpResponseHandler {
    http_status_t status;
    StringMap headers;
    IstreamPointer body;

    std::string body_string;

    GError *error = nullptr;

    bool done = false;

public:
    explicit GlueHttpRequest(struct pool &pool)
        :headers(pool), body(nullptr) {}

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
        return {status, std::move(headers), std::move(body_string)};
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

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t _status, StringMap &&_headers,
                        Istream *_body) override {
        assert(error == nullptr);

        status = _status;
        headers = std::move(_headers);

        if (_body != nullptr) {
            body.Set(*_body, *this);
            body.Read();
        } else
            done = true;
    }

    void OnHttpError(GError *_error) override {
        assert(error == nullptr);

        error = _error;
        done = true;
    }
};

GlueHttpResponse
GlueHttpClient::Request(EventLoop &event_loop,
                        struct pool &p, GlueHttpServerAddress &server,
                        http_method_t method, const char *uri,
                        ConstBuffer<void> body)
{
    CancellablePointer cancel_ptr;

    GlueHttpRequest request(p);
    Request(p, event_loop, server, method, uri, body,
            request, cancel_ptr);
    while (!request.IsDone() && event_loop.LoopOnce()) {}

    request.CheckThrowError();

    return request.MoveResponse();
}
