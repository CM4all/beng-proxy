/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ForwardHttpRequest.hxx"
#include "HttpConnection.hxx"
#include "ClusterConfig.hxx"
#include "ListenerConfig.hxx"
#include "lb_instance.hxx"
#include "lb_session.hxx"
#include "lb_cookie.hxx"
#include "lb_jvm_route.hxx"
#include "lb_headers.hxx"
#include "ssl/ssl_filter.hxx"
#include "address_sticky.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_server/Handler.hxx"
#include "http_client.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "http_response.hxx"
#include "http_headers.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "lease.hxx"
#include "strmap.hxx"
#include "failure.hxx"
#include "bulldog.hxx"
#include "pool.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketDescriptor.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "util/Cancellable.hxx"
#include "util/LeakDetector.hxx"
#include "util/djbhash.h"

class LbRequest final
    : LeakDetector, Cancellable, StockGetHandler, Lease, HttpResponseHandler {

    LbHttpConnection &connection;
    LbCluster &cluster;
    const LbClusterConfig &cluster_config;

    TcpBalancer &balancer;

    HttpServerRequest &request;

    /**
     * The request body.
     */
    UnusedHoldIstreamPtr body;

    CancellablePointer cancel_ptr;

    /**
     * The address we're currently connecting to (waiting for
     * #StockGetHandler to be called).  Only used for Zeroconf to be
     * able to call failure_set().
     */
    SocketAddress current_address;

    StockItem *stock_item;

    unsigned new_cookie = 0;

    bool response_sent = false, reuse;

    enum class LeaseState : uint8_t {
        NONE,
        BUSY,
        PENDING,
    } lease_state = LeaseState::NONE;

public:
    LbRequest(LbHttpConnection &_connection, LbCluster &_cluster,
              TcpBalancer &_balancer,
              HttpServerRequest &_request,
              CancellablePointer &_cancel_ptr)
        :connection(_connection), cluster(_cluster),
         cluster_config(cluster.GetConfig()),
         balancer(_balancer),
         request(_request),
         body(request.pool, request.body) {
        _cancel_ptr = *this;
    }

    void Start();

private:
    void Destroy() {
        assert(lease_state == LeaseState::NONE);

        DeleteFromPool(request.pool, this);
    }

    void DoRelease() {
        assert(lease_state == LeaseState::PENDING);

        lease_state = LeaseState::NONE;
        stock_item->Put(!reuse);
    }

    bool CheckRelease() {
        if (lease_state == LeaseState::PENDING)
            DoRelease();
        return lease_state == LeaseState::NONE;
    }

    void ResponseSent() {
        assert(!response_sent);
        response_sent = true;

        if (CheckRelease())
            Destroy();
    }

    const char *GetCanonicalHost() const;

    sticky_hash_t GetStickyHash();
    sticky_hash_t GetHostHash() const;
    sticky_hash_t MakeCookieHash();

    /* virtual methods from class Cancellable */
    void Cancel() override {
        assert(!response_sent);

        /* this pool reference is necessary because
           cancel_ptr.Cancel() may release the only remaining
           reference on the pool */
        const ScopePoolRef ref(request.pool TRACE_ARGS);

        body.Clear();
        cancel_ptr.Cancel();

        CheckRelease();
        Destroy();
    }

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(std::exception_ptr ep) override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override;

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(std::exception_ptr ep) override;
};

static void
SendResponse(HttpServerRequest &request,
             const LbSimpleHttpResponse &response)
{
    assert(response.IsDefined());

    http_server_simple_response(request, response.status,
                                response.location.empty() ? nullptr : response.location.c_str(),
                                response.message.empty() ? nullptr : response.message.c_str());
}

static bool
send_fallback(HttpServerRequest &request,
              const LbSimpleHttpResponse &fallback)
{
    if (!fallback.IsDefined())
        return false;

    SendResponse(request, fallback);
    return true;
}

inline const char *
LbRequest::GetCanonicalHost() const
{
    return connection.canonical_host != nullptr
        ? connection.canonical_host
        : request.headers.Get("host");
}

inline sticky_hash_t
LbRequest::GetHostHash() const
{
    const char *host = GetCanonicalHost();
    if (host == nullptr)
        return 0;

    return djb_hash_string(host);
}

/**
 * Generate a cookie for sticky worker selection.  Return only worker
 * numbers that are not known to be failing.  Returns 0 on total
 * failure.
 */
static unsigned
generate_cookie(const AddressList *list)
{
    assert(list->GetSize() >= 2);

    const unsigned first = lb_cookie_generate(list->GetSize());

    unsigned i = first;
    do {
        assert(i >= 1 && i <= list->GetSize());
        const SocketAddress address = list->addresses[i % list->GetSize()];
        if (failure_get_status(address) == FAILURE_OK &&
            bulldog_check(address) && !bulldog_is_fading(address))
            return i;

        i = lb_cookie_next(list->GetSize(), i);
    } while (i != first);

    /* all nodes have failed */
    return first;
}

sticky_hash_t
LbRequest::MakeCookieHash()
{
    unsigned hash = lb_cookie_get(request.headers);
    if (hash == 0)
        new_cookie = hash = generate_cookie(&cluster_config.address_list);

    return hash;
}

sticky_hash_t
LbRequest::GetStickyHash()
{
    switch (cluster_config.sticky_mode) {
    case StickyMode::NONE:
    case StickyMode::FAILOVER:
        /* these modes require no preparation; they are handled
           completely by balancer_get() */
        return 0;

    case StickyMode::SOURCE_IP:
        /* calculate session_sticky from remote address */
        return socket_address_sticky(request.remote_address);

    case StickyMode::HOST:
        /* calculate session_sticky from "Host" request header */
        return GetHostHash();

    case StickyMode::SESSION_MODULO:
        /* calculate session_sticky from beng-proxy session id */
        return lb_session_get(request.headers,
                              cluster_config.session_cookie.c_str());

    case StickyMode::COOKIE:
        /* calculate session_sticky from beng-lb cookie */
        return MakeCookieHash();

    case StickyMode::JVM_ROUTE:
        /* calculate session_sticky from JSESSIONID cookie suffix */
        return lb_jvm_route_get(request.headers, cluster_config);
    }

    assert(false);
    gcc_unreachable();
}

/*
 * HTTP response handler
 *
 */

void
LbRequest::OnHttpResponse(http_status_t status, StringMap &&_headers,
                          Istream *response_body)
{
    assert(lease_state != LeaseState::NONE);
    assert(!response_sent);

    failure_unset(tcp_stock_item_get_address(*stock_item), FAILURE_RESPONSE);

    HttpHeaders headers(std::move(_headers));

    if (request.method == HTTP_METHOD_HEAD)
        /* pass Content-Length, even though there is no response body
           (RFC 2616 14.13) */
        headers.MoveToBuffer("content-length");

    if (new_cookie != 0) {
        char buffer[64];
        /* "Discard" must be last, to work around an Android bug*/
        snprintf(buffer, sizeof(buffer),
                 "beng_lb_node=0-%x; HttpOnly; Path=/; Version=1; Discard",
                 new_cookie);

        headers.Write("cookie2", "$Version=\"1\"");
        headers.Write("set-cookie", buffer);
    }

    http_server_response(&request, status, std::move(headers), response_body);
    ResponseSent();
}

void
LbRequest::OnHttpError(std::exception_ptr ep)
{
    assert(lease_state != LeaseState::NONE);
    assert(!response_sent);

    if (IsHttpClientServerFailure(ep))
        failure_add(tcp_stock_item_get_address(*stock_item));

    connection.logger(2, ep);

    if (!send_fallback(request, cluster_config.fallback))
        connection.SendError(request, ep);

    ResponseSent();
}

/*
 * stock callback
 *
 */

void
LbRequest::OnStockItemReady(StockItem &item)
{
    assert(lease_state == LeaseState::NONE);
    assert(!response_sent);

    if (cluster_config.HasZeroConf())
        /* without the tcp_balancer, we have to roll our own failure
           updates */
        failure_unset(current_address, FAILURE_FAILED);

    stock_item = &item;
    lease_state = LeaseState::BUSY;

    const char *peer_subject = connection.ssl_filter != nullptr
        ? ssl_filter_get_peer_subject(connection.ssl_filter)
        : nullptr;
    const char *peer_issuer_subject = connection.ssl_filter != nullptr
        ? ssl_filter_get_peer_issuer_subject(connection.ssl_filter)
        : nullptr;

    auto &headers = request.headers;
    lb_forward_request_headers(request.pool, headers,
                               request.local_host_and_port,
                               request.remote_host,
                               peer_subject, peer_issuer_subject,
                               cluster_config.mangle_via);

    http_client_request(request.pool,
                        connection.instance.event_loop,
                        tcp_stock_item_get(item),
                        tcp_stock_item_get_domain(item) == AF_LOCAL
                        ? FdType::FD_SOCKET : FdType::FD_TCP,
                        *this,
                        item.GetStockName(),
                        NULL, NULL,
                        request.method, request.uri,
                        HttpHeaders(std::move(headers)),
                        body.Steal(), true,
                        *this, cancel_ptr);
}

void
LbRequest::OnStockItemError(std::exception_ptr ep)
{
    assert(lease_state == LeaseState::NONE);
    assert(!response_sent);

    connection.logger(2, "Connect error: ", ep);

    if (cluster_config.HasZeroConf())
        /* without the tcp_balancer, we have to roll our own failure
           updates */
        failure_add(current_address);

    body.Clear();

    if (!send_fallback(request, cluster_config.fallback))
        connection.SendError(request, ep);

    ResponseSent();
}

/*
 * Lease
 *
 */

void
LbRequest::ReleaseLease(bool _reuse)
{
    assert(lease_state == LeaseState::BUSY);

    lease_state = LeaseState::PENDING;
    reuse = _reuse;

    if (response_sent) {
        DoRelease();
        Destroy();
    }
}

/*
 * constructor
 *
 */

inline void
LbRequest::Start()
{
    SocketAddress bind_address = SocketAddress::Null();
    const bool transparent_source = cluster_config.transparent_source;
    if (transparent_source) {
        bind_address = request.remote_address;

        /* reset the port to 0 to allow the kernel to choose one */
        if (bind_address.GetFamily() == AF_INET) {
            struct sockaddr_in *s_in = (struct sockaddr_in *)
                p_memdup(&request.pool, bind_address.GetAddress(),
                         bind_address.GetSize());
            s_in->sin_port = 0;
            bind_address = SocketAddress((const struct sockaddr *)s_in,
                                         bind_address.GetSize());
        } else if (bind_address.GetFamily() == AF_INET6) {
            struct sockaddr_in6 *s_in = (struct sockaddr_in6 *)
                p_memdup(&request.pool, bind_address.GetAddress(),
                         bind_address.GetSize());
            s_in->sin6_port = 0;
            bind_address = SocketAddress((const struct sockaddr *)s_in,
                                         bind_address.GetSize());
        }
    }

    if (cluster_config.HasZeroConf()) {
        const auto member = cluster.Pick(GetStickyHash());
        if (member.first == nullptr) {
            const ScopePoolRef ref(request.pool TRACE_ARGS);
            http_server_send_message(&request,
                                     HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                     "Zeroconf cluster is empty");
            Destroy();
            return;
        }

        assert(member.second.IsDefined());

        current_address = member.second;

        tcp_stock_get(*connection.instance.tcp_stock, request.pool,
                      member.first,
                      transparent_source, bind_address,
                      member.second,
                      20,
                      *this, cancel_ptr);

        return;
    }

    tcp_balancer_get(balancer, request.pool,
                     transparent_source,
                     bind_address,
                     GetStickyHash(),
                     cluster_config.address_list,
                     20,
                     *this, cancel_ptr);
}

void
ForwardHttpRequest(LbHttpConnection &connection,
                   HttpServerRequest &request,
                   LbCluster &cluster,
                   CancellablePointer &cancel_ptr)
{
    const auto request2 =
        NewFromPool<LbRequest>(request.pool,
                               connection, cluster,
                               *connection.instance.tcp_balancer,
                               request, cancel_ptr);
    request2->Start();
}
