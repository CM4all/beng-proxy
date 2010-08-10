/*
 * Handler for control messages.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control-handler.h"
#include "control-server.h"
#include "instance.h"

#include <daemon/log.h>

#include <assert.h>
#include <arpa/inet.h>

static void
global_control_packet(enum beng_control_command command,
                      const void *payload, size_t payload_length,
                      void *ctx)
{
    struct instance *instance = ctx;

    (void)payload;
    (void)instance;

    daemon_log(5, "control command=%d payload_length=%zu\n",
               command, payload_length);
}

static const struct control_handler global_control_handler = {
    .packet = global_control_packet,
};

static struct control_server *global_control_server;

bool
global_control_handler_init(pool_t pool, struct instance *instance)
{
    if (instance->config.control_listen == NULL)
        return true;

    struct in_addr group;
    if (instance->config.multicast_group != NULL)
        group.s_addr = inet_addr(instance->config.multicast_group);

    global_control_server =
        control_server_new(pool, instance->config.control_listen, 5478,
                           instance->config.multicast_group != NULL
                           ? &group : NULL,
                           &global_control_handler, instance);
    return global_control_server != NULL;
}

void
global_control_handler_deinit(void)
{
    if (global_control_server != NULL)
        control_server_free(global_control_server);
}
