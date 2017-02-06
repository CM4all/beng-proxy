/*
 * Glue code for using the ssl_filter in a client connection.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_client.hxx"
#include "ssl_config.hxx"
#include "Basic.hxx"
#include "Ctx.hxx"
#include "ssl_filter.hxx"
#include "ssl_quark.hxx"
#include "Error.hxx"
#include "thread_socket_filter.hxx"
#include "thread_pool.hxx"
#include "pool.hxx"
#include "util/ScopeExit.hxx"

#include <daemon/log.h>

static SslCtx ssl_client_ctx;

void
ssl_client_init()
{
    try {
        ssl_client_ctx = CreateBasicSslCtx(false);
    } catch (const SslError &e) {
        daemon_log(1, "ssl_factory_new() failed: %s\n", e.what());
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

static void *
ssl_client_create2(struct pool *pool, EventLoop &event_loop,
                   const char *hostname)
{
    UniqueSSL ssl(SSL_new(ssl_client_ctx.get()));
    if (!ssl)
        throw SslError("SSL_new() failed");

    SSL_set_connect_state(ssl.get());

    (void)hostname; // TODO: use this parameter

    auto f = ssl_filter_new(*pool, std::move(ssl));

    auto &queue = thread_pool_get_queue(event_loop);
    return thread_socket_filter_new(*pool, event_loop, queue,
                                    &ssl_filter_get_handler(*f));
}

void *
ssl_client_create(struct pool *pool, EventLoop &event_loop,
                  const char *hostname)
{
    /* create a new pool for the SSL filter; this is necessary because
       thread_socket_filter_close() may need to invoke
       pool_set_persistent(), which is only possible if nobody else
       has "trashed" the pool yet */
    pool = pool_new_linear(pool, "ssl_client", 1024);
    AtScopeExit(pool) { pool_unref(pool); };

    return ssl_client_create2(pool, event_loop, hostname);
}
