/*
 * Listener on a control datagram socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_control.hxx"
#include "lb_instance.hxx"
#include "lb_config.hxx"
#include "lb_stats.hxx"
#include "control_server.h"
#include "address_envelope.h"
#include "address_edit.h"
#include "failure.h"
#include "tpool.h"

#include <daemon/log.h>
#include <socket/address.h>

#include <string.h>
#include <stdlib.h>

static void
enable_node(const struct lb_instance *instance,
          const char *payload, size_t length)
{
    const char *colon = (const char *)memchr(payload, ':', length);
    if (colon == NULL || colon == payload || colon == payload + length - 1) {
        daemon_log(3, "malformed FADE_NODE control packet: no port\n");
        return;
    }

    const AutoRewindPool auto_rewind(tpool);

    char *node_name = p_strndup(tpool, payload, length);
    char *port_string = node_name + (colon - payload);
    *port_string++ = 0;

    const lb_node_config *node = instance->config->FindNode(node_name);
    if (node == NULL) {
        daemon_log(3, "unknown node in FADE_NODE control packet\n");
        return;
    }

    char *endptr;
    unsigned port = strtoul(port_string, &endptr, 10);
    if (port == 0 || *endptr != 0) {
        daemon_log(3, "malformed FADE_NODE control packet: port is not a number\n");
        return;
    }

    const struct sockaddr *with_port =
        sockaddr_set_port(tpool,
                          &node->envelope->address, node->envelope->length,
                          port);

    char buffer[64];
    socket_address_to_string(buffer, sizeof(buffer), with_port,
                             node->envelope->length);
    daemon_log(4, "enabling node %s (%s)\n", node_name, buffer);

    failure_unset(with_port, node->envelope->length, FAILURE_OK);
}

static void
fade_node(const struct lb_instance *instance,
          const char *payload, size_t length)
{
    const char *colon = (const char *)memchr(payload, ':', length);
    if (colon == NULL || colon == payload || colon == payload + length - 1) {
        daemon_log(3, "malformed FADE_NODE control packet: no port\n");
        return;
    }

    const AutoRewindPool auto_rewind(tpool);

    char *node_name = p_strndup(tpool, payload, length);
    char *port_string = node_name + (colon - payload);
    *port_string++ = 0;

    const lb_node_config *node = instance->config->FindNode(node_name);
    if (node == NULL) {
        daemon_log(3, "unknown node in FADE_NODE control packet\n");
        return;
    }

    char *endptr;
    unsigned port = strtoul(port_string, &endptr, 10);
    if (port == 0 || *endptr != 0) {
        daemon_log(3, "malformed FADE_NODE control packet: port is not a number\n");
        return;
    }

    const struct sockaddr *with_port =
        sockaddr_set_port(tpool,
                          &node->envelope->address, node->envelope->length,
                          port);

    char buffer[64];
    socket_address_to_string(buffer, sizeof(buffer), with_port,
                             node->envelope->length);
    daemon_log(4, "fading node %s (%s)\n", node_name, buffer);

    /* set status "FADE" for 3 hours */
    failure_set(with_port, node->envelope->length, FAILURE_FADE, 3 * 3600);
}

G_GNUC_CONST
static const char *
failure_status_to_string(enum failure_status status)
{
    switch (status) {
    case FAILURE_OK:
        return "ok";

    case FAILURE_FADE:
        return "fade";

    case FAILURE_RESPONSE:
    case FAILURE_FAILED:
    case FAILURE_MONITOR:
        break;
    }

    return "error";
}

static bool
node_status_response(struct control_server *server, struct pool *pool,
                     const struct sockaddr *address, size_t address_length,
                     const char *payload, size_t length, const char *status,
                     GError **error_r)
{
    size_t status_length = strlen(status);

    size_t response_length = length + 1 + status_length;
    char *response = PoolAlloc<char>(tpool, response_length);
    memcpy(response, payload, length);
    response[length] = 0;
    memcpy(response + length + 1, status, status_length);

    return control_server_reply(server, pool, address, address_length,
                                CONTROL_NODE_STATUS, response, response_length,
                                error_r);
}

static void
query_node_status(struct lb_control *control,
                  const char *payload, size_t length,
                  const struct sockaddr *address, size_t address_length)
{
    if (address_length == 0) {
        daemon_log(3, "got NODE_STATUS from unbound client socket\n");
        return;
    }

    const char *colon = (const char *)memchr(payload, ':', length);
    if (colon == NULL || colon == payload || colon == payload + length - 1) {
        node_status_response(control->server, tpool, address, address_length,
                             payload, length, "malformed", NULL);
        daemon_log(3, "malformed NODE_STATUS control packet: no port\n");
        return;
    }

    const AutoRewindPool auto_rewind(tpool);

    char *node_name = p_strndup(tpool, payload, length);
    char *port_string = node_name + (colon - payload);
    *port_string++ = 0;

    const lb_node_config *node =
        control->instance->config->FindNode(node_name);
    if (node == NULL) {
        node_status_response(control->server, tpool, address, address_length,
                             payload, length, "unknown", NULL);
        daemon_log(3, "unknown node in NODE_STATUS control packet\n");
        return;
    }

    char *endptr;
    unsigned port = strtoul(port_string, &endptr, 10);
    if (port == 0 || *endptr != 0) {
        node_status_response(control->server, tpool, address, address_length,
                             payload, length, "malformed", NULL);
        daemon_log(3, "malformed NODE_STATUS control packet: port is not a number\n");
        return;
    }

    const struct sockaddr *with_port =
        sockaddr_set_port(tpool,
                          &node->envelope->address, node->envelope->length,
                          port);

    char buffer[64];
    socket_address_to_string(buffer, sizeof(buffer), with_port,
                             node->envelope->length);

    enum failure_status status =
        failure_get_status(with_port, node->envelope->length);
    const char *s = failure_status_to_string(status);

    GError *error = NULL;
    if (!node_status_response(control->server, tpool, address, address_length,
                              payload, length, s,
                              &error)) {
        daemon_log(3, "%s\n", error->message);
        g_error_free(error);
    }
}

static void
query_stats(struct lb_control *control,
            const struct sockaddr *address, size_t address_length)
{
    struct beng_control_stats stats;
    lb_get_stats(control->instance, &stats);

    const AutoRewindPool auto_rewind(tpool);

    GError *error = NULL;
    if (!control_server_reply(control->server, tpool, address, address_length,
                              CONTROL_STATS, &stats, sizeof(stats),
                              &error)) {
        daemon_log(3, "%s\n", error->message);
        g_error_free(error);
    }
}

static void
lb_control_packet(enum beng_control_command command,
                  const void *payload, size_t payload_length,
                  const struct sockaddr *address, size_t address_length,
                  void *ctx)
{
    struct lb_control *control = (struct lb_control *)ctx;

    switch (command) {
    case CONTROL_NOP:
    case CONTROL_TCACHE_INVALIDATE:
        break;

    case CONTROL_ENABLE_NODE:
        enable_node(control->instance, (const char *)payload, payload_length);
        break;

    case CONTROL_FADE_NODE:
        fade_node(control->instance, (const char *)payload, payload_length);
        break;

    case CONTROL_NODE_STATUS:
        query_node_status(control, (const char *)payload, payload_length,
                          address, address_length);
        break;

    case CONTROL_DUMP_POOLS:
        pool_dump_tree(control->instance->pool);
        break;

    case CONTROL_STATS:
        query_stats(control, address, address_length);
        break;

    case CONTROL_VERBOSE:
        if (payload_length == 1)
            daemon_log_config.verbose = *(const uint8_t *)payload;
        break;
    }
}

static void
lb_control_error(GError *error, G_GNUC_UNUSED void *ctx)
{
    daemon_log(2, "%s\n", error->message);
    g_error_free(error);
}

static constexpr struct control_handler lb_control_handler = {
    nullptr,
    lb_control_packet,
    lb_control_error,
};

struct lb_control *
lb_control_new(struct lb_instance *instance,
               const struct lb_control_config *config,
               GError **error_r)
{
    struct pool *pool = pool_new_linear(instance->pool, "lb_control", 1024);

    lb_control *control = NewFromPool<lb_control>(pool);
    control->pool = pool;
    control->instance = instance;

    control->server =
        control_server_new(pool, &config->envelope->address,
                           config->envelope->length,
                           &lb_control_handler, control,
                           error_r);
    if (control->server == NULL) {
        pool_unref(pool);
        return NULL;
    }

    return control;
}

void
lb_control_free(struct lb_control *control)
{
    control_server_free(control->server);

    pool_unref(control->pool);
}

void
lb_control_enable(struct lb_control *control)
{
    control_server_enable(control->server);
}

void
lb_control_disable(struct lb_control *control)
{
    control_server_disable(control->server);
}
