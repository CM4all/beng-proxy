/*
 * Connect to one of a list of addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "client_balancer.hxx"
#include "generic_balancer.hxx"
#include "net/ConnectSocket.hxx"
#include "address_list.hxx"
#include "balancer.hxx"
#include "net/StaticSocketAddress.hxx"

struct ClientBalancerRequest : ConnectSocketHandler {
    EventLoop &event_loop;

    bool ip_transparent;
    StaticSocketAddress bind_address;

    /**
     * The connect timeout for each attempt.
     */
    unsigned timeout;

    ConnectSocketHandler &handler;

    ClientBalancerRequest(EventLoop &_event_loop,
                          bool _ip_transparent, SocketAddress _bind_address,
                          unsigned _timeout,
                          ConnectSocketHandler &_handler)
        :event_loop(_event_loop), ip_transparent(_ip_transparent),
         timeout(_timeout),
         handler(_handler) {
        if (_bind_address.IsNull() || !_bind_address.IsDefined())
            bind_address.Clear();
        else
            bind_address = _bind_address;
    }

    void Send(struct pool &pool, SocketAddress address,
              CancellablePointer &cancel_ptr);

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) override;
    void OnSocketConnectTimeout() override;
    void OnSocketConnectError(std::exception_ptr ep) override;
};

inline void
ClientBalancerRequest::Send(struct pool &pool, SocketAddress address,
                            CancellablePointer &cancel_ptr)
{
    client_socket_new(event_loop, pool,
                      address.GetFamily(), SOCK_STREAM, 0,
                      ip_transparent,
                      bind_address,
                      address,
                      timeout,
                      *this,
                      cancel_ptr);
}

/*
 * client_socket_handler
 *
 */

void
ClientBalancerRequest::OnSocketConnectSuccess(UniqueSocketDescriptor &&fd)
{
    auto &base = BalancerRequest<ClientBalancerRequest>::Cast(*this);
    base.Success();

    handler.OnSocketConnectSuccess(std::move(fd));
}

void
ClientBalancerRequest::OnSocketConnectTimeout()
{
    auto &base = BalancerRequest<ClientBalancerRequest>::Cast(*this);
    if (!base.Failure())
        handler.OnSocketConnectTimeout();
}

void
ClientBalancerRequest::OnSocketConnectError(std::exception_ptr ep)
{
    auto &base = BalancerRequest<ClientBalancerRequest>::Cast(*this);
    if (!base.Failure())
        handler.OnSocketConnectError(ep);
}

/*
 * constructor
 *
 */

void
client_balancer_connect(EventLoop &event_loop,
                        struct pool &pool, Balancer &balancer,
                        bool ip_transparent,
                        SocketAddress bind_address,
                        unsigned session_sticky,
                        const AddressList *address_list,
                        unsigned timeout,
                        ConnectSocketHandler &handler,
                        CancellablePointer &cancel_ptr)
{
    BalancerRequest<ClientBalancerRequest>::Start(pool, balancer,
                                                  *address_list,
                                                  cancel_ptr,
                                                  session_sticky,
                                                  event_loop,
                                                  ip_transparent,
                                                  bind_address,
                                                  timeout,
                                                  handler);
}
