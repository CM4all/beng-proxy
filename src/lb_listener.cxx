/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_listener.hxx"
#include "lb_instance.hxx"
#include "lb_connection.hxx"
#include "lb_config.hxx"
#include "notify.h"
#include "ssl_factory.hxx"
#include "listener.h"
#include "address_envelope.h"
#include "pool.h"

#include <daemon/log.h>

static void
lb_listener_notify_callback(void *ctx)
{
    struct lb_listener *listener = (struct lb_listener *)ctx;
    (void)listener;
    /* XXX check SSL events */
}

/*
 * listener_handler
 *
 */

static void
lb_listener_connected(int fd,
                      const struct sockaddr *address, size_t address_length,
                      void *ctx)
{
    struct lb_listener *listener = (struct lb_listener *)ctx;

    lb_connection_new(listener->instance, listener->config,
                      listener->ssl_factory, listener->notify,
                      fd, address, address_length);
}

static void
lb_listener_error(GError *error, G_GNUC_UNUSED void *ctx)
{
    daemon_log(2, "%s\n", error->message);
    g_error_free(error);
}

const struct listener_handler lb_listener_handler = {
    .connected = lb_listener_connected,
    .error = lb_listener_error,
};

/*
 * constructor
 *
 */

struct lb_listener *
lb_listener_new(struct lb_instance *instance,
                const struct lb_listener_config *config,
                GError **error_r)
{
    struct pool *pool = pool_new_linear(instance->pool, "lb_listener", 8192);

    struct lb_listener *listener =
        (struct lb_listener *)p_malloc(pool, sizeof(*listener));
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

        listener->ssl_factory = ssl_factory_new(pool, &config->ssl_config,
                                                error_r);
        if (listener->ssl_factory == NULL) {
            notify_free(listener->notify);
            pool_unref(pool);
            return NULL;
        }
    } else {
        listener->notify = NULL;
        listener->ssl_factory = NULL;
    }

    const struct address_envelope *envelope = config->envelope;
    listener->listener = listener_new(pool, envelope->address.sa_family,
                                      SOCK_STREAM, 0, &envelope->address,
                                      envelope->length,
                                      &lb_listener_handler, listener,
                                      error_r);
    if (listener->listener == NULL) {
        if (listener->ssl_factory != NULL)
            ssl_factory_free(listener->ssl_factory);
        pool_unref(pool);
        return NULL;
    }

    return listener;
}

void
lb_listener_free(struct lb_listener *listener)
{
    listener_free(&listener->listener);

    if (listener->ssl_factory != NULL)
        ssl_factory_free(listener->ssl_factory);

    if (listener->notify != NULL)
        notify_free(listener->notify);

    pool_unref(listener->pool);
}

void
lb_listener_event_add(struct lb_listener *listener)
{
    listener_event_add(listener->listener);
}

void
lb_listener_event_del(struct lb_listener *listener)
{
    listener_event_del(listener->listener);
}
