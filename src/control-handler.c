/*
 * Handler for control messages.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control-handler.h"
#include "control-server.h"
#include "udp-listener.h"
#include "udp-distribute.h"
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

    switch (command) {
    case CONTROL_NOP:
        /* duh! */
        break;
    }
}

static const struct control_handler global_control_handler = {
    .packet = global_control_packet,
};

static struct udp_distribute *global_udp_distribute;

static void
global_control_udp_callback(const void *data, size_t length,
                            G_GNUC_UNUSED const struct sockaddr *addr,
                            G_GNUC_UNUSED size_t addrlen,
                            void *ctx)
{
    struct instance *instance = ctx;

    /* forward the packet to all worker processes */
    udp_distribute_packet(global_udp_distribute, data, length);

    /* handle the packet in this process */
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

    global_udp_distribute = udp_distribute_new(pool);

    return true;
}

void
global_control_handler_deinit(void)
{
    if (global_udp_distribute != NULL)
        udp_distribute_free(global_udp_distribute);

    if (global_udp_listener != NULL)
        udp_listener_free(global_udp_listener);
}

int
global_control_handler_add_fd(void)
{
    assert(global_udp_listener != NULL);
    assert(global_udp_distribute != NULL);

    return udp_distribute_add(global_udp_distribute);
}

void
global_control_handler_set_fd(int fd)
{
    assert(global_udp_listener != NULL);
    assert(global_udp_distribute != NULL);

    udp_distribute_clear(global_udp_distribute);
    udp_listener_set_fd(global_udp_listener, fd);
}
