/*
 * Connect to one of a list of addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "client_balancer.hxx"
#include "client-socket.h"
#include "address_envelope.hxx"
#include "address_list.hxx"
#include "balancer.hxx"
#include "failure.hxx"
#include "async.h"
#include "pool.h"

struct client_balancer_request {
    struct pool *pool;
    struct balancer *balancer;

    bool ip_transparent;
    const struct sockaddr *bind_address;
    size_t bind_address_size;

    /**
     * The "sticky id" of the incoming HTTP request.
     */
    unsigned session_sticky;

    /**
     * The connect timeout for each attempt.
     */
    unsigned timeout;

    /**
     * The number of remaining connection attempts.  We give up when
     * we get an error and this attribute is already zero.
     */
    unsigned retries;

    const struct address_list *address_list;
    const struct address_envelope *current_address;

    const struct client_socket_handler *handler;
    void *handler_ctx;

    struct async_operation_ref *async_ref;
};

extern const struct client_socket_handler client_balancer_socket_handler;

static void
client_balancer_next(struct client_balancer_request *request)
{
    const struct address_envelope &envelope =
        balancer_get(*request->balancer, *request->address_list,
                     request->session_sticky);
    request->current_address = &envelope;

    client_socket_new(request->pool,
                      envelope.address.sa_family, SOCK_STREAM, 0,
                      request->ip_transparent,
                      request->bind_address, request->bind_address_size,
                      &envelope.address, envelope.length,
                      request->timeout,
                      &client_balancer_socket_handler, request,
                      request->async_ref);
}

/*
 * client_socket_handler
 *
 */

static void
client_balancer_socket_success(int fd, void *ctx)
{
    struct client_balancer_request *request =
        (struct client_balancer_request *)ctx;

    failure_unset(&request->current_address->address,
                  request->current_address->length,
                  FAILURE_FAILED);

    request->handler->success(fd, request->handler_ctx);
}

static void
client_balancer_socket_timeout(void *ctx)
{
    struct client_balancer_request *request =
        (struct client_balancer_request *)ctx;

    failure_add(&request->current_address->address,
                request->current_address->length);

    if (request->retries-- > 0)
        /* try again, next address */
        client_balancer_next(request);
    else
        /* give up */
        request->handler->timeout(request->handler_ctx);
}

static void
client_balancer_socket_error(GError *error, void *ctx)
{
    struct client_balancer_request *request =
        (struct client_balancer_request *)ctx;

    failure_add(&request->current_address->address,
                request->current_address->length);

    if (request->retries-- > 0) {
        /* try again, next address */
        g_error_free(error);

        client_balancer_next(request);
    } else
        /* give up */
        request->handler->error(error, request->handler_ctx);
}

const struct client_socket_handler client_balancer_socket_handler = {
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
                        const struct sockaddr *bind_address,
                        size_t bind_address_size,
                        unsigned session_sticky,
                        const struct address_list *address_list,
                        unsigned timeout,
                        const struct client_socket_handler *handler, void *ctx,
                        struct async_operation_ref *async_ref)
{
    auto request = NewFromPool<struct client_balancer_request>(pool);
    request->pool = pool;
    request->balancer = balancer;
    request->ip_transparent = ip_transparent;
    request->bind_address = bind_address != nullptr
        ? (struct sockaddr *)p_memdup(pool, bind_address, bind_address_size)
        : nullptr;
    request->bind_address_size = bind_address_size;
    request->session_sticky = session_sticky;
    request->timeout = timeout;

    if (address_list->GetSize() <= 1)
        request->retries = 0;
    else if (address_list->GetSize() == 2)
        request->retries = 1;
    else if (address_list->GetSize() == 3)
        request->retries = 2;
    else
        request->retries = 3;

    request->address_list = address_list;
    request->handler = handler;
    request->handler_ctx = ctx;
    request->async_ref = async_ref;

    client_balancer_next(request);
}
