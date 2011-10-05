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
#include "tcache.h"
#include "translate.h"
#include "tpool.h"
#include "beng-proxy/translation.h"

#include <daemon/log.h>

#include <glib.h>
#include <assert.h>
#include <arpa/inet.h>

static bool
apply_translation_packet(struct translate_request *request,
                         enum beng_translation_command command,
                         const char *payload)
{
    switch (command) {
    case TRANSLATE_URI:
        request->uri = payload;
        break;

    case TRANSLATE_SESSION:
        request->session = payload;
        break;

        /* XXX
    case TRANSLATE_LOCAL_ADDRESS:
        request->local_address = payload;
        break;
        */

    case TRANSLATE_REMOTE_HOST:
        request->remote_host = payload;
        break;

    case TRANSLATE_HOST:
        request->host = payload;
        break;

    case TRANSLATE_LANGUAGE:
        request->accept_language = payload;
        break;

    case TRANSLATE_USER_AGENT:
        request->user_agent = payload;
        break;

    case TRANSLATE_QUERY_STRING:
        request->query_string = payload;
        break;

    default:
        /* unsupported */
        return false;
    }

    return true;
}

static unsigned
decode_translation_packets(struct pool *pool, struct translate_request *request,
                           uint16_t *cmds, unsigned max_cmds,
                           const void *data, size_t length,
                           const char **site_r)
{
    *site_r = NULL;

    unsigned num_cmds = 0;

    if (length % 4 != 0)
        /* must be padded */
        return 0;

    while (length > 0) {
        const struct beng_translation_header *header = data;
        if (length < sizeof(*header))
            return 0;

        size_t payload_length = GUINT16_FROM_BE(header->length);
        enum beng_translation_command command =
            GUINT16_FROM_BE(header->command);

        data = header + 1;
        length -= sizeof(*header);

        if (length < payload_length)
            return 0;

        char *payload = payload_length > 0
            ? p_strndup(pool, data, payload_length)
            : NULL;
        if (command == TRANSLATE_SITE)
            *site_r = payload;
        else if (apply_translation_packet(request, command, payload)) {
            if (num_cmds >= max_cmds)
                return 0;

            cmds[num_cmds++] = (uint16_t)command;
        } else
            return 0;

        payload_length = ((payload_length + 3) | 3) - 3; /* apply padding */

        data = (const char *)data + payload_length;
        length -= payload_length;
    }

    return num_cmds;
}

static void
control_tcache_invalidate(struct instance *instance,
                          const void *payload, size_t payload_length)
{
    (void)instance;

    struct pool_mark mark;
    pool_mark(tpool, &mark);

    struct translate_request request;
    memset(&request, 0, sizeof(request));

    const char *site;
    uint16_t cmds[32];
    unsigned num_cmds =
        decode_translation_packets(tpool, &request, cmds, G_N_ELEMENTS(cmds),
                                   payload, payload_length, &site);
    if (num_cmds == 0 && site == NULL) {
        pool_rewind(tpool, &mark);
        daemon_log(2, "malformed TCACHE_INVALIDATE control packet\n");
        return;
    }

    translate_cache_invalidate(instance->translate_cache, &request,
                               cmds, num_cmds,
                               site);

    pool_rewind(tpool, &mark);
}

static void
global_control_packet(enum beng_control_command command,
                      const void *payload, size_t payload_length,
                      G_GNUC_UNUSED const struct sockaddr *address,
                      G_GNUC_UNUSED size_t address_length,
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

    case CONTROL_TCACHE_INVALIDATE:
        control_tcache_invalidate(instance, payload, payload_length);
        break;

    case CONTROL_ENABLE_NODE:
    case CONTROL_FADE_NODE:
    case CONTROL_NODE_STATUS:
        /* only for beng-lb */
        break;
    }
}

static void
global_control_error(GError *error, G_GNUC_UNUSED void *ctx)
{
    daemon_log(2, "%s\n", error->message);
    g_error_free(error);
}

static const struct control_handler global_control_handler = {
    .packet = global_control_packet,
    .error = global_control_error,
};

static struct udp_distribute *global_udp_distribute;

static void
global_control_udp_datagram(const void *data, size_t length,
                            const struct sockaddr *address,
                            size_t address_length,
                            void *ctx)
{
    struct instance *instance = ctx;

    /* forward the packet to all worker processes */
    udp_distribute_packet(global_udp_distribute, data, length);

    /* handle the packet in this process */
    control_server_decode(data, length, address, address_length,
                          &global_control_handler, instance);
}

static void
global_control_udp_error(GError *error, G_GNUC_UNUSED void *ctx)
{
    daemon_log(2, "%s\n", error->message);
    g_error_free(error);
}

static const struct udp_handler global_control_udp_handler = {
    .datagram = global_control_udp_datagram,
    .error = global_control_udp_error,
};

static struct udp_listener *global_udp_listener;

bool
global_control_handler_init(struct pool *pool, struct instance *instance)
{
    if (instance->config.control_listen == NULL)
        return true;

    GError *error = NULL;
    global_udp_listener =
        udp_listener_port_new(pool, instance->config.control_listen, 5478,
                              &global_control_udp_handler, instance, &error);
    if (global_udp_listener == NULL) {
        daemon_log(1, "%s\n", error->message);
        g_error_free(error);
        return false;
    }

    if (instance->config.multicast_group != NULL) {
        const struct in_addr group = {
            .s_addr = inet_addr(instance->config.multicast_group),
        };

        if (!udp_listener_join4(global_udp_listener, &group, &error)) {
            udp_listener_free(global_udp_listener);
            daemon_log(1, "%s\n", error->message);
            g_error_free(error);
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
