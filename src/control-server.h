/*
 * Server side part of the "control" protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_SERVER_H
#define BENG_PROXY_CONTROL_SERVER_H

#include "beng-proxy/control.h"
#include "pool.h"

#include <stddef.h>

struct in_addr;

struct control_handler {
    void (*packet)(enum beng_control_command command,
                   const void *payload, size_t payload_length,
                   void *ctx);
};

struct control_server;

struct control_server *
control_server_new(pool_t pool, const char *host_and_port, int default_port,
                   const struct in_addr *group,
                   const struct control_handler *handler, void *ctx);

void
control_server_free(struct control_server *cs);

void
control_server_decode(const void *data, size_t length,
                      const struct control_handler *handler, void *handler_ctx);

#endif
