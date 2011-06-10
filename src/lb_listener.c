/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_listener.h"
#include "lb_instance.h"
#include "lb_connection.h"
#include "lb_config.h"
#include "notify.h"
#include "ssl_create.h"
#include "listener.h"
#include "address-envelope.h"

static void
lb_listener_notify_callback(void *ctx)
{
    struct lb_listener *listener = ctx;
    (void)listener;
    /* XXX check SSL events */
}

static void
lb_listener_callback(int fd,
                     const struct sockaddr *address, size_t address_length,
                     void *ctx)
{
    struct lb_listener *listener = ctx;

    lb_connection_new(listener->instance, listener->config,
                      listener->ssl_ctx, listener->notify,
                      fd, address, address_length);
}

struct lb_listener *
lb_listener_new(struct lb_instance *instance,
                const struct lb_listener_config *config,
                GError **error_r)
{
    struct pool *pool = pool_new_linear(instance->pool, "lb_listener", 8192);

    struct lb_listener *listener = p_malloc(pool, sizeof(*listener));
    listener->pool = pool;
    listener->instance = instance;
    listener->config = config;

    if (config->ssl) {
        /* prepare SSL support */

        listener->notify = notify_new(pool, lb_listener_notify_callback,
                                      listener, error_r);
        if (listener->notify == NULL) {
            pool_unref(pool);
            return NULL;
        }

        listener->ssl_ctx = ssl_create(&config->ssl_config, error_r);
        if (listener->ssl_ctx == NULL) {
            notify_free(listener->notify);
            pool_unref(pool);
            return NULL;
        }
    } else {
        listener->notify = NULL;
        listener->ssl_ctx = NULL;
    }

    const struct address_envelope *envelope = config->envelope;
    listener->listener = listener_new(pool, envelope->address.sa_family,
                                      SOCK_STREAM, 0, &envelope->address,
                                      envelope->length,
                                      lb_listener_callback, listener,
                                      error_r);
    if (listener->listener == NULL) {
        if (listener->ssl_ctx != NULL)
            SSL_CTX_free(listener->ssl_ctx);
        pool_unref(pool);
        return NULL;
    }

    return listener;
}

void
lb_listener_free(struct lb_listener *listener)
{
    listener_free(&listener->listener);

    if (listener->ssl_ctx != NULL)
        SSL_CTX_free(listener->ssl_ctx);

    if (listener->notify != NULL)
        notify_free(listener->notify);

    pool_unref(listener->pool);
}
