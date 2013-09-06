/*
 * Handler for raw TCP connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_tcp.hxx"
#include "address_list.h"
#include "sink_fd.h"
#include "client-balancer.h"
#include "client-socket.h"
#include "istream-socket.h"
#include "address_sticky.h"
#include "async.h"

#include <unistd.h>

struct lb_tcp {
    struct pool *pool;
    struct stock *pipe_stock;

    const struct lb_tcp_handler *handler;
    void *handler_ctx;

    struct {
        int fd;

        enum istream_direct type;

        struct sink_fd *sink;
    } peers[2];

    struct async_operation_ref connect;
};

/*
 * first istream_socket handler
 *
 */

static void
first_istream_socket_read(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    sink_fd_read(tcp->peers[0].sink);
}

static void
first_istream_socket_close(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    (void)tcp;
}

static bool
first_istream_socket_error(int error, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->handler->_errno("Receive failed", error, tcp->handler_ctx);
    return false;
}

static bool
first_istream_socket_depleted(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    (void)tcp;
    return true;
}

static bool
first_istream_socket_finished(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->handler->eof(tcp->handler_ctx);
    return false;
}

static constexpr struct istream_socket_handler first_istream_socket_handler = {
    nullptr,
    first_istream_socket_read,
    first_istream_socket_close,
    first_istream_socket_error,
    first_istream_socket_depleted,
    first_istream_socket_finished,
};

/*
 * second istream_socket handler
 *
 */

static void
second_istream_socket_read(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    sink_fd_read(tcp->peers[1].sink);
}

static void
second_istream_socket_close(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    (void)tcp;
}

static bool
second_istream_socket_error(int error, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->handler->_errno("Receive failed", error, tcp->handler_ctx);
    return false;
}

static bool
second_istream_socket_depleted(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    (void)tcp;
    return true;
}

static bool
second_istream_socket_finished(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->handler->eof(tcp->handler_ctx);
    return false;
}

static const struct istream_socket_handler second_istream_socket_handler = {
    nullptr,
    second_istream_socket_read,
    second_istream_socket_close,
    second_istream_socket_error,
    second_istream_socket_depleted,
    second_istream_socket_finished,
};

/*
 * first sink_fd handler
 *
 */

static void
first_sink_input_eof(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->handler->eof(tcp->handler_ctx);
}

static void
first_sink_input_error(GError *error, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->peers[0].sink = NULL;

    tcp->handler->gerror("Error", error, tcp->handler_ctx);
}

static bool
first_sink_send_error(int error, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->peers[0].sink = NULL;

    tcp->handler->_errno("Send failed", error, tcp->handler_ctx);
    return false;
}

static const struct sink_fd_handler first_sink_fd_handler = {
    .input_eof = first_sink_input_eof,
    .input_error = first_sink_input_error,
    .send_error = first_sink_send_error,
};

/*
 * second sink_fd handler
 *
 */

static void
second_sink_input_eof(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->handler->eof(tcp->handler_ctx);
}

static void
second_sink_input_error(GError *error, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->peers[1].sink = NULL;

    tcp->handler->gerror("Error", error, tcp->handler_ctx);
}

static bool
second_sink_send_error(int error, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    tcp->peers[1].sink = NULL;

    tcp->handler->_errno("Send failed", error, tcp->handler_ctx);
    return false;
}

static const struct sink_fd_handler second_sink_fd_handler = {
    .input_eof = second_sink_input_eof,
    .input_error = second_sink_input_error,
    .send_error = second_sink_send_error,
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

    tcp->peers[1].fd = fd;

    struct istream *istream =
        istream_socket_new(tcp->pool,
                           tcp->peers[0].fd,
                           tcp->peers[0].type,
                           &first_istream_socket_handler, tcp);
    istream = istream_pipe_new(tcp->pool, istream, tcp->pipe_stock);

    tcp->peers[1].sink =
        sink_fd_new(tcp->pool, istream, fd, ISTREAM_TCP,
                    &second_sink_fd_handler, tcp);

    istream = istream_socket_new(tcp->pool, fd, ISTREAM_TCP,
                                 &second_istream_socket_handler, tcp);
    istream = istream_pipe_new(tcp->pool, istream, tcp->pipe_stock);

    tcp->peers[0].sink =
        sink_fd_new(tcp->pool, istream,
                    tcp->peers[0].fd,
                    tcp->peers[0].type,
                    &first_sink_fd_handler, tcp);
}

static void
lb_tcp_client_socket_timeout(void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    close(tcp->peers[0].fd);

    tcp->handler->error("Connect error", "Timeout", tcp->handler_ctx);
}

static void
lb_tcp_client_socket_error(GError *error, void *ctx)
{
    struct lb_tcp *tcp = (struct lb_tcp *)ctx;

    close(tcp->peers[0].fd);

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
           const struct sockaddr *remote_address,
           const struct address_list &address_list,
           struct balancer &balancer,
           const struct lb_tcp_handler *handler, void *ctx,
           lb_tcp **tcp_r)
{
    lb_tcp *tcp = (lb_tcp *)p_malloc(pool, sizeof(*tcp));
    tcp->pool = pool;
    tcp->pipe_stock = pipe_stock;
    tcp->handler = handler;
    tcp->handler_ctx = ctx;
    tcp->peers[0].fd = fd;
    tcp->peers[0].type = fd_type;

    unsigned session_sticky = lb_tcp_sticky(address_list, remote_address);

    *tcp_r = tcp;

    client_balancer_connect(pool, &balancer,
                            session_sticky,
                            &address_list,
                            20,
                            &lb_tcp_client_socket_handler, tcp,
                            &tcp->connect);
}

void
lb_tcp_close(struct lb_tcp *tcp)
{
    if (async_ref_defined(&tcp->connect))
        async_abort(&tcp->connect);
    else {
        if (tcp->peers[0].sink != NULL)
            sink_fd_close(tcp->peers[0].sink);

        if (tcp->peers[1].sink != NULL)
            sink_fd_close(tcp->peers[1].sink);

        close(tcp->peers[0].fd);
        close(tcp->peers[1].fd);
    }
}
