/*
 * Handler for control messages.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control-handler.h"
#include "control-server.h"
#include "udp-listener.h"
#include "instance.h"

#include <daemon/log.h>

#include <glib.h>
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

static void
global_control_udp_callback(const void *data, size_t length,
                            G_GNUC_UNUSED const struct sockaddr *addr,
                            G_GNUC_UNUSED size_t addrlen,
                            void *ctx)
{
    struct instance *instance = ctx;

    control_server_decode(data, length, &global_control_handler, instance);
}

static struct udp_listener *global_udp_listener;

bool
global_control_handler_init(pool_t pool, struct instance *instance)
{
    if (instance->config.control_listen == NULL)
        return true;

    global_udp_listener =
        udp_listener_port_new(pool, instance->config.control_listen, 5478,
                              global_control_udp_callback, instance);
    if (global_udp_listener == NULL)
        return false;

    if (instance->config.multicast_group != NULL) {
        const struct in_addr group = {
            .s_addr = inet_addr(instance->config.multicast_group),
        };

        if (!udp_listener_join4(global_udp_listener, &group)) {
            udp_listener_free(global_udp_listener);
            return false;
        }
    }

    return true;
}

void
global_control_handler_deinit(void)
{
    if (global_udp_listener != NULL)
        udp_listener_free(global_udp_listener);
}
