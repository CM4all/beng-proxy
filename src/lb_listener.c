/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_listener.h"
#include "lb_instance.h"
#include "lb_connection.h"
#include "lb_config.h"
#include "listener.h"
#include "address-envelope.h"

static void
lb_listener_callback(int fd,
                     const struct sockaddr *address, size_t address_length,
                     void *ctx)
{
    struct lb_listener *listener = ctx;

    connection_new(listener->instance, listener->config,
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

    const struct address_envelope *envelope = config->envelope;
    listener->listener = listener_new(pool, envelope->address.sa_family,
                                      SOCK_STREAM, 0, &envelope->address,
                                      envelope->length,
                                      lb_listener_callback, listener,
                                      error_r);
    if (listener->listener == NULL) {
        pool_unref(pool);
        return NULL;
    }

    return listener;
}

void
lb_listener_free(struct lb_listener *listener)
{
    listener_free(&listener->listener);

    pool_unref(listener->pool);
}
