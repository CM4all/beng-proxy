/*
 * Server side part of the "control" protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_SERVER_H
#define BENG_PROXY_CONTROL_SERVER_H

#include "beng-proxy/control.h"
#include "pool.h"

#include <glib.h>

#include <stddef.h>

struct address_envelope;
struct in_addr;

struct control_handler {
    void (*packet)(enum beng_control_command command,
                   const void *payload, size_t payload_length,
                   void *ctx);

    void (*error)(GError *error, void *ctx);
};

G_GNUC_CONST
static inline GQuark
control_server_quark(void)
{
    return g_quark_from_static_string("control_server");
}

struct control_server *
control_server_new(pool_t pool, const char *host_and_port, int default_port,
                   const struct in_addr *group,
                   const struct control_handler *handler, void *ctx,
                   GError **error_r);

struct control_server *
control_server_new_envelope(pool_t pool,
                            const struct address_envelope *envelope,
                            const struct control_handler *handler, void *ctx,
                            GError **error_r);

void
control_server_free(struct control_server *cs);

void
control_server_decode(const void *data, size_t length,
                      const struct control_handler *handler, void *handler_ctx);

#endif
