/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_listener.hxx"
#include "lb_instance.hxx"
#include "lb_connection.hxx"
#include "lb_config.hxx"
#include "ssl_factory.hxx"
#include "listener.hxx"
#include "address_envelope.hxx"
#include "pool.h"
#include "util/Error.hxx"

#include <daemon/log.h>

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

    lb_connection_new(&listener->instance, &listener->config,
                      listener->ssl_factory,
                      fd, address, address_length);
}

static void
lb_listener_error(Error &&error, G_GNUC_UNUSED void *ctx)
{
    daemon_log(2, "%s\n", error.GetMessage());
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
lb_listener_new(struct lb_instance &instance,
                const struct lb_listener_config &config,
                Error &error)
{
    lb_listener *listener = new lb_listener(instance, config);

    if (config.ssl) {
        /* prepare SSL support */

        listener->ssl_factory = ssl_factory_new(config.ssl_config, true,
                                                error);
        if (listener->ssl_factory == NULL) {
            delete listener;
            return NULL;
        }
    }

    const struct address_envelope *envelope = config.envelope;

    listener->listener = listener_new(envelope->address.sa_family,
                                      SOCK_STREAM, 0, &envelope->address,
                                      envelope->length,
                                      &lb_listener_handler, listener,
                                      error);
    if (listener->listener == NULL) {
        if (listener->ssl_factory != NULL)
            ssl_factory_free(listener->ssl_factory);
        delete listener;
        return NULL;
    }

    return listener;
}

lb_listener::~lb_listener()
{
    if (listener != nullptr)
        listener_free(&listener);

    if (ssl_factory != nullptr)
        ssl_factory_free(ssl_factory);
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
