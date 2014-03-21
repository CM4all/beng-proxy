/*
 * Handler for raw TCP connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_tcp.hxx"
#include "filtered_socket.h"
#include "address_list.h"
#include "client-balancer.h"
#include "client-socket.h"
#include "address_sticky.h"
#include "async.h"
#include "direct.h"

#include <unistd.h>
#include <errno.h>

struct lb_tcp {
    struct pool *pool;
    struct stock *pipe_stock;

    const struct lb_tcp_handler *handler;
    void *handler_ctx;

    struct filtered_socket inbound;

    struct buffered_socket outbound;

    struct async_operation_ref connect;
};

static constexpr timeval write_timeout = { 30, 0 };

static void
lb_tcp_destroy_inbound(struct lb_tcp *tcp)
{
    if (filtered_socket_connected(&tcp->inbound))
        filtered_socket_close(&tcp->inbound);

    filtered_socket_destroy(&tcp->inbound);
}

static void
lb_tcp_destroy_outbound(struct lb_tcp *tcp)
{
    if (buffered_socket_connected(&tcp->outbound))
        buffered_socket_close(&tcp->outbound);

    buffered_socket_destroy(&tcp->outbound);
}

/*
 * inbound buffered_socket_handler
 *
 */

static enum buffered_result
inbound_buffered_socket_data(const void *buffer, size_t size, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    if (async_ref_defined(&tcp->connect))
        /* outbound is not yet connected */
        return BUFFERED_BLOCKING;

    if (!buffered_socket_valid(&tcp->outbound)) {
        lb_tcp_close(tcp);
        tcp->handler->error("Send error", "Broken socket", tcp->handler_ctx);
        return BUFFERED_CLOSED;
    }

    ssize_t nbytes = buffered_socket_write(&tcp->outbound, buffer, size);
    if (nbytes > 0) {
        filtered_socket_consumed(&tcp->inbound, nbytes);
        return (size_t)nbytes == size
            ? BUFFERED_OK
            : BUFFERED_PARTIAL;
    }

    switch ((enum write_result)nbytes) {
    case WRITE_SOURCE_EOF:
        assert(false);
        gcc_unreachable();

    case WRITE_ERRNO:
        lb_tcp_close(tcp);
        tcp->handler->_errno("Send failed", errno, tcp->handler_ctx);
        return BUFFERED_CLOSED;

    case WRITE_BLOCKING:
        return BUFFERED_BLOCKING;

    case WRITE_DESTROYED:
        return BUFFERED_CLOSED;

    case WRITE_BROKEN:
        lb_tcp_close(tcp);
        tcp->handler->eof(tcp->handler_ctx);
        return BUFFERED_CLOSED;
    }

    assert(false);
    gcc_unreachable();
}

static bool
inbound_buffered_socket_closed(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->eof(tcp->handler_ctx);
    return false;
}

static bool
inbound_buffered_socket_write(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    return buffered_socket_read(&tcp->outbound, false);
}

static bool
inbound_buffered_socket_drained(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    if (!buffered_socket_valid(&tcp->outbound)) {
        /* now that inbound's output buffers are drained, we can
           finally close the connection (postponed from
           outbound_buffered_socket_end()) */
        lb_tcp_close(tcp);
        tcp->handler->eof(tcp->handler_ctx);
        return false;
    }

    return true;
}

static bool
inbound_buffered_socket_broken(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->eof(tcp->handler_ctx);
    return false;
}

static void
inbound_buffered_socket_error(GError *error, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->gerror("Error", error, tcp->handler_ctx);
}

static constexpr struct buffered_socket_handler inbound_buffered_socket_handler = {
    inbound_buffered_socket_data,
    nullptr, // TODO: inbound_buffered_socket_direct,
    inbound_buffered_socket_closed,
    nullptr,
    nullptr,
    inbound_buffered_socket_write,
    inbound_buffered_socket_drained,
    nullptr,
    inbound_buffered_socket_broken,
    inbound_buffered_socket_error,
};

/*
 * outbound buffered_socket_handler
 *
 */

static enum buffered_result
outbound_buffered_socket_data(const void *buffer, size_t size, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    ssize_t nbytes = filtered_socket_write(&tcp->inbound, buffer, size);
    if (nbytes > 0) {
        buffered_socket_consumed(&tcp->outbound, nbytes);
        return (size_t)nbytes == size
            ? BUFFERED_OK
            : BUFFERED_PARTIAL;
    }

    switch ((enum write_result)nbytes) {
    case WRITE_SOURCE_EOF:
        assert(false);
        gcc_unreachable();

    case WRITE_ERRNO:
        lb_tcp_close(tcp);
        tcp->handler->_errno("Send failed", errno, tcp->handler_ctx);
        return BUFFERED_CLOSED;

    case WRITE_BLOCKING:
        return BUFFERED_BLOCKING;

    case WRITE_DESTROYED:
        return BUFFERED_CLOSED;

    case WRITE_BROKEN:
        lb_tcp_close(tcp);
        tcp->handler->eof(tcp->handler_ctx);
        return BUFFERED_CLOSED;
    }

    assert(false);
    gcc_unreachable();
}

static bool
outbound_buffered_socket_closed(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    buffered_socket_close(&tcp->outbound);
    return true;
}

static void
outbound_buffered_socket_end(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    buffered_socket_destroy(&tcp->outbound);

    if (filtered_socket_is_drained(&tcp->inbound)) {
        /* all output buffers to "inbound" are drained; close the
           connection, because there's nothing left to do */
        lb_tcp_close(tcp);
        tcp->handler->eof(tcp->handler_ctx);

        /* nothing will be done if the buffers are not yet drained;
           we're waiting for inbound_buffered_socket_drained() to be
           called */
    }
}

static bool
outbound_buffered_socket_write(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    return filtered_socket_read(&tcp->inbound, false);
}

static bool
outbound_buffered_socket_broken(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->eof(tcp->handler_ctx);
    return false;
}

static void
outbound_buffered_socket_error(GError *error, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->gerror("Error", error, tcp->handler_ctx);
}

static constexpr struct buffered_socket_handler outbound_buffered_socket_handler = {
    outbound_buffered_socket_data,
    nullptr, // TODO: outbound_buffered_socket_direct,
    outbound_buffered_socket_closed,
    nullptr,
    outbound_buffered_socket_end,
    outbound_buffered_socket_write,
    nullptr,
    nullptr,
    outbound_buffered_socket_broken,
    outbound_buffered_socket_error,
};

/*
 * stock_handler
 *
 */

static void
lb_tcp_client_socket_success(int fd, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    async_ref_clear(&tcp->connect);

    buffered_socket_init(&tcp->outbound, tcp->pool,
                         fd, ISTREAM_TCP,
                         nullptr, &write_timeout,
                         &outbound_buffered_socket_handler, tcp);

    /* TODO
    tcp->outbound.direct = tcp->pipe_stock != nullptr &&
        (ISTREAM_TO_TCP & ISTREAM_PIPE) != 0 &&
        (istream_direct_mask_to(tcp->inbound.base.base.fd_type) & ISTREAM_PIPE) != 0;
    */

    if (filtered_socket_read(&tcp->inbound, false))
        buffered_socket_read(&tcp->outbound, false);
}

static void
lb_tcp_client_socket_timeout(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_destroy_inbound(tcp);
    tcp->handler->error("Connect error", "Timeout", tcp->handler_ctx);
}

static void
lb_tcp_client_socket_error(GError *error, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    lb_tcp_destroy_inbound(tcp);
    tcp->handler->gerror("Connect error", error, tcp->handler_ctx);
}

static const struct client_socket_handler lb_tcp_client_socket_handler = {
    .success = lb_tcp_client_socket_success,
    .timeout = lb_tcp_client_socket_timeout,
    .error = lb_tcp_client_socket_error,
};

/*
 * constructor
 *
 */

gcc_pure
static unsigned
lb_tcp_sticky(const struct address_list &address_list,
              const struct sockaddr *remote_address)
{
    switch (address_list.sticky_mode) {
    case STICKY_NONE:
    case STICKY_FAILOVER:
        break;

    case STICKY_SOURCE_IP:
        return socket_address_sticky(remote_address);

    case STICKY_SESSION_MODULO:
    case STICKY_COOKIE:
    case STICKY_JVM_ROUTE:
        /* not implemented here */
        break;
    }

    return 0;
}

void
lb_tcp_new(struct pool *pool, struct stock *pipe_stock,
           int fd, enum istream_direct fd_type,
           const struct socket_filter *filter, void *filter_ctx,
           const struct sockaddr *remote_address,
           size_t remote_address_size,
           bool transparent_source,
           const struct address_list &address_list,
           struct balancer &balancer,
           const struct lb_tcp_handler *handler, void *ctx,
           lb_tcp **tcp_r)
{
    lb_tcp *tcp = NewFromPool<lb_tcp>(pool);
    tcp->pool = pool;
    tcp->pipe_stock = pipe_stock;
    tcp->handler = handler;
    tcp->handler_ctx = ctx;

    filtered_socket_init(&tcp->inbound, pool, fd, fd_type,
                         nullptr, &write_timeout,
                         filter, filter_ctx,
                         &inbound_buffered_socket_handler, tcp);
    /* TODO
    tcp->inbound.base.direct = pipe_stock != nullptr &&
        (ISTREAM_TO_PIPE & fd_type) != 0 &&
        (ISTREAM_TO_TCP & ISTREAM_PIPE) != 0;
    */

    unsigned session_sticky = lb_tcp_sticky(address_list, remote_address);

    const struct sockaddr *bind_address = nullptr;
    size_t bind_address_size = 0;

    if (transparent_source) {
        bind_address = remote_address;
        bind_address_size = remote_address_size;

        /* reset the port to 0 to allow the kernel to choose one */
        if (bind_address->sa_family == AF_INET) {
            struct sockaddr_in *s_in = (struct sockaddr_in *)
                p_memdup(pool, bind_address, bind_address_size);
            s_in->sin_port = 0;
            bind_address = (const struct sockaddr *)s_in;
        } else if (bind_address->sa_family == AF_INET6) {
            struct sockaddr_in6 *s_in = (struct sockaddr_in6 *)
                p_memdup(pool, bind_address, bind_address_size);
            s_in->sin6_port = 0;
            bind_address = (const struct sockaddr *)s_in;
        }
    }

    *tcp_r = tcp;

    client_balancer_connect(pool, &balancer,
                            transparent_source,
                            bind_address, bind_address_size,
                            session_sticky,
                            &address_list,
                            20,
                            &lb_tcp_client_socket_handler, tcp,
                            &tcp->connect);
}

void
lb_tcp_close(struct lb_tcp *tcp)
{
    if (filtered_socket_valid(&tcp->inbound))
        lb_tcp_destroy_inbound(tcp);

    if (async_ref_defined(&tcp->connect))
        async_abort(&tcp->connect);
    else if (buffered_socket_valid(&tcp->outbound))
        lb_tcp_destroy_outbound(tcp);
}
