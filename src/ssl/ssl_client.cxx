/*
 * Glue code for using the ssl_filter in a client connection.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_client.hxx"
#include "ssl_config.hxx"
#include "ssl_filter.hxx"
#include "ssl/Basic.hxx"
#include "ssl/Ctx.hxx"
#include "ssl/Error.hxx"
#include "io/Logger.hxx"
#include "thread_socket_filter.hxx"
#include "thread_pool.hxx"
#include "util/ScopeExit.hxx"

static SslCtx ssl_client_ctx;

void
ssl_client_init()
{
    try {
        ssl_client_ctx = CreateBasicSslCtx(false);
    } catch (const SslError &e) {
        LogConcat(1, "ssl_client", "ssl_factory_new() failed: ", e.what());
    }
}

void
ssl_client_deinit()
{
    ssl_client_ctx.reset();
}

const SocketFilter &
ssl_client_get_filter()
{
    return thread_socket_filter;;
}

void *
ssl_client_create(EventLoop &event_loop,
                  const char *hostname)
{
    UniqueSSL ssl(SSL_new(ssl_client_ctx.get()));
    if (!ssl)
        throw SslError("SSL_new() failed");

    SSL_set_connect_state(ssl.get());

    (void)hostname; // TODO: use this parameter

    auto f = ssl_filter_new(std::move(ssl));

    auto &queue = thread_pool_get_queue(event_loop);
    return new ThreadSocketFilter(event_loop, queue,
                                  &ssl_filter_get_handler(*f));
}
