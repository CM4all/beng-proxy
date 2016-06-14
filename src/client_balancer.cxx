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

#include <glib.h>

struct ClientBalancerRequest : ConnectSocketHandler {
    bool ip_transparent;
    StaticSocketAddress bind_address;

    /**
     * The connect timeout for each attempt.
     */
    unsigned timeout;

    ConnectSocketHandler &handler;

    ClientBalancerRequest(bool _ip_transparent, SocketAddress _bind_address,
                          unsigned _timeout,
                          ConnectSocketHandler &_handler)
        :ip_transparent(_ip_transparent),
         timeout(_timeout),
         handler(_handler) {
        if (_bind_address.IsNull())
            bind_address.Clear();
        else
            bind_address = _bind_address;
    }

    void Send(struct pool &pool, SocketAddress address,
              struct async_operation_ref &async_ref);

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(SocketDescriptor &&fd) override;
    void OnSocketConnectTimeout() override;
    void OnSocketConnectError(GError *error) override;
};

inline void
ClientBalancerRequest::Send(struct pool &pool, SocketAddress address,
                            struct async_operation_ref &async_ref)
{
    client_socket_new(pool,
                      address.GetFamily(), SOCK_STREAM, 0,
                      ip_transparent,
                      bind_address,
                      address,
                      timeout,
                      *this,
                      async_ref);
}

/*
 * client_socket_handler
 *
 */

void
ClientBalancerRequest::OnSocketConnectSuccess(SocketDescriptor &&fd)
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
ClientBalancerRequest::OnSocketConnectError(GError *error)
{
    auto &base = BalancerRequest<ClientBalancerRequest>::Cast(*this);
    if (!base.Failure())
        handler.OnSocketConnectError(error);
    else
        g_error_free(error);
}

/*
 * constructor
 *
 */

void
client_balancer_connect(struct pool *pool, Balancer *balancer,
                        bool ip_transparent,
                        SocketAddress bind_address,
                        unsigned session_sticky,
                        const AddressList *address_list,
                        unsigned timeout,
                        ConnectSocketHandler &handler,
                        struct async_operation_ref *async_ref)
{
    BalancerRequest<ClientBalancerRequest>::Start(*pool, *balancer,
                                                  *address_list,
                                                  *async_ref,
                                                  session_sticky,
                                                  ip_transparent,
                                                  bind_address,
                                                  timeout,
                                                  handler);
}
