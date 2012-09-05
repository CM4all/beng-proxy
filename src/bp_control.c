/*
 * Handler for control messages.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_control.h"
#include "control-server.h"
#include "control-server.h"
#include "udp-distribute.h"
#include "instance.h"
#include "tcache.h"
#include "translate-request.h"
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

    case TRANSLATE_UA_CLASS:
        request->ua_class = payload;
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

    case CONTROL_DUMP_POOLS:
        pool_dump_tree(instance->pool);
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

static struct udp_distribute *global_udp_distribute;

static void
global_control_raw(const void *data, size_t length,
                   gcc_unused const struct sockaddr *address,
                   gcc_unused size_t address_length,
                   gcc_unused void *ctx)
{
    /* forward the packet to all worker processes */
    udp_distribute_packet(global_udp_distribute, data, length);
}

static const struct control_handler global_control_handler = {
    .raw = global_control_raw,
    .packet = global_control_packet,
    .error = global_control_error,
};

bool
global_control_handler_init(struct pool *pool, struct instance *instance)
{
    if (instance->config.control_listen == NULL)
        return true;

    struct in_addr group_buffer;
    const struct in_addr *group = NULL;
    if (instance->config.multicast_group != NULL) {
        group_buffer.s_addr = inet_addr(instance->config.multicast_group);
        group = &group_buffer;
    }

    GError *error = NULL;
    instance->control_server =
        control_server_new(pool, instance->config.control_listen, 5478,
                           group,
                           &global_control_handler, instance,
                           &error);
    if (instance->control_server == NULL) {
        daemon_log(1, "%s\n", error->message);
        g_error_free(error);
        return false;
    }

    global_udp_distribute = udp_distribute_new(pool);

    return true;
}

void
global_control_handler_deinit(struct instance *instance)
{
    if (global_udp_distribute != NULL)
        udp_distribute_free(global_udp_distribute);

    if (instance->control_server != NULL)
        control_server_free(instance->control_server);
}

int
global_control_handler_add_fd(struct instance *instance)
{
    assert(instance->control_server != NULL);
    assert(global_udp_distribute != NULL);

    return udp_distribute_add(global_udp_distribute);
}

void
global_control_handler_set_fd(struct instance *instance, int fd)
{
    assert(instance->control_server != NULL);
    assert(global_udp_distribute != NULL);

    udp_distribute_clear(global_udp_distribute);
    control_server_set_fd(instance->control_server, fd);
}
