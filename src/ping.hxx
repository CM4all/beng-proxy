/*
 * Sending ICMP echo-request messages (ping).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PING_HXX
#define BENG_PING_HXX

#include <glib.h>

#include <stddef.h>

struct ping_handler {
    void (*response)(void *ctx);
    void (*timeout)(void *ctx);
    void (*error)(GError *error, void *ctx);
};

struct pool;
class SocketAddress;
struct async_operation_ref;

G_GNUC_CONST
static inline GQuark
ping_quark(void)
{
    return g_quark_from_static_string("ping");
}

/**
 * Is the "ping" client available?
 */
G_GNUC_CONST
bool
ping_available(void);

/**
 * Sends a "ping" to the server, and waits for the reply.
 */
void
ping(struct pool *pool, SocketAddress address,
     const struct ping_handler *handler, void *ctx,
     struct async_operation_ref *async_ref);

#endif
