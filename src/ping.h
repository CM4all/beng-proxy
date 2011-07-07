/*
 * Sending ICMP echo-request messages (ping).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PING_H
#define BENG_PING_H

#include <glib.h>
#include <stdbool.h>

struct ping_handler {
    void (*response)(void *ctx);
    void (*timeout)(void *ctx);
    void (*error)(GError *error, void *ctx);
};

struct pool;
struct address_envelope;
struct async_operation_ref;

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
ping(struct pool *pool, const struct address_envelope *envelope,
     const struct ping_handler *handler, void *ctx,
     struct async_operation_ref *async_ref);

#endif
