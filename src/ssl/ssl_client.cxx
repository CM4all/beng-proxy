/*
 * Glue code for using the ssl_filter in a client connection.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_client.hxx"
#include "ssl_config.hxx"
#include "Basic.hxx"
#include "ssl_filter.hxx"
#include "ssl_quark.hxx"
#include "Error.hxx"
#include "thread_socket_filter.hxx"
#include "thread_pool.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <glib.h>

static SSL_CTX *ssl_client_ctx;

void
ssl_client_init()
{
    try {
        ssl_client_ctx = CreateBasicSslCtx(false).release();
    } catch (const SslError &e) {
        daemon_log(1, "ssl_factory_new() failed: %s\n", e.what());
    }
}

void
ssl_client_deinit()
{
    if (ssl_client_ctx != nullptr)
        SSL_CTX_free(ssl_client_ctx);
}

const SocketFilter &
ssl_client_get_filter()
{
    return thread_socket_filter;;
}

static void *
ssl_client_create2(struct pool *pool,
                   const char *hostname,
                   GError **error_r)
{
    UniqueSSL ssl(SSL_new(ssl_client_ctx));
    if (!ssl) {
        g_set_error_literal(error_r, ssl_quark(), 0, "SSL_new() failed");
        return nullptr;
    }

    SSL_set_connect_state(ssl.get());

    (void)hostname; // TODO: use this parameter

    auto f = ssl_filter_new(*pool, std::move(ssl));

    auto &queue = thread_pool_get_queue();
    return thread_socket_filter_new(*pool, queue,
                                    ssl_thread_socket_filter, f);
}

void *
ssl_client_create(struct pool *pool,
                  const char *hostname,
                  GError **error_r)
{
    /* create a new pool for the SSL filter; this is necessary because
       thread_socket_filter_close() may need to invoke
       pool_set_persistent(), which is only possible if nobody else
       has "trashed" the pool yet */
    pool = pool_new_linear(pool, "ssl_client", 1024);

    void *result = ssl_client_create2(pool, hostname, error_r);
    pool_unref(pool);
    return result;
}
