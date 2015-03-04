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

struct client_balancer_request {
    bool ip_transparent;
    StaticSocketAddress bind_address;

    /**
     * The connect timeout for each attempt.
     */
    unsigned timeout;

    const ConnectSocketHandler *handler;
    void *handler_ctx;

    client_balancer_request(bool _ip_transparent, SocketAddress _bind_address,
                            unsigned _timeout,
                            const ConnectSocketHandler &_handler,
                            void *_handler_ctx)
        :ip_transparent(_ip_transparent),
         timeout(_timeout),
         handler(&_handler), handler_ctx(_handler_ctx) {
        if (_bind_address.IsNull())
            bind_address.Clear();
        else
            bind_address = _bind_address;
    }

    void Send(struct pool &pool, SocketAddress address,
              struct async_operation_ref &async_ref);
};

extern const ConnectSocketHandler client_balancer_socket_handler;

inline void
client_balancer_request::Send(struct pool &pool, SocketAddress address,
                              struct async_operation_ref &async_ref)
{
    client_socket_new(pool,
                      address.GetFamily(), SOCK_STREAM, 0,
                      ip_transparent,
                      bind_address,
                      address,
                      timeout,
                      client_balancer_socket_handler, this,
                      async_ref);
}

/*
 * client_socket_handler
 *
 */

static void
client_balancer_socket_success(SocketDescriptor &&fd, void *ctx)
{
    struct client_balancer_request *request =
        (struct client_balancer_request *)ctx;

    auto &base = BalancerRequest<struct client_balancer_request>::Cast(*request);
    base.Success();

    request->handler->success(std::move(fd), request->handler_ctx);
}

static void
client_balancer_socket_timeout(void *ctx)
{
    struct client_balancer_request *request =
        (struct client_balancer_request *)ctx;

    auto &base = BalancerRequest<struct client_balancer_request>::Cast(*request);
    if (!base.Failure())
        request->handler->timeout(request->handler_ctx);
}

static void
client_balancer_socket_error(GError *error, void *ctx)
{
    struct client_balancer_request *request =
        (struct client_balancer_request *)ctx;

    auto &base = BalancerRequest<struct client_balancer_request>::Cast(*request);
    if (!base.Failure())
        request->handler->error(error, request->handler_ctx);
}

const ConnectSocketHandler client_balancer_socket_handler = {
    .success = client_balancer_socket_success,
    .timeout = client_balancer_socket_timeout,
    .error = client_balancer_socket_error,
};

/*
 * constructor
 *
 */

void
client_balancer_connect(struct pool *pool, struct balancer *balancer,
                        bool ip_transparent,
                        SocketAddress bind_address,
                        unsigned session_sticky,
                        const AddressList *address_list,
                        unsigned timeout,
                        const ConnectSocketHandler *handler, void *ctx,
                        struct async_operation_ref *async_ref)
{
    BalancerRequest<struct client_balancer_request>::Start(*pool,
                                                           *balancer,
                                                           *address_list,
                                                           *async_ref,
                                                           session_sticky,
                                                           ip_transparent,
                                                           bind_address,
                                                           timeout,
                                                           *handler, ctx);
}
