/*
 * Listener on a control datagram socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_control.hxx"
#include "lb_instance.hxx"
#include "lb_config.hxx"
#include "lb_stats.hxx"
#include "control_server.hxx"
#include "address_edit.h"
#include "failure.hxx"
#include "tpool.hxx"
#include "pool.hxx"

#include <daemon/log.h>
#include <socket/address.h>

#include <string.h>
#include <stdlib.h>

static void
enable_node(const struct lb_instance *instance,
          const char *payload, size_t length)
{
    const char *colon = (const char *)memchr(payload, ':', length);
    if (colon == nullptr || colon == payload || colon == payload + length - 1) {
        daemon_log(3, "malformed FADE_NODE control packet: no port\n");
        return;
    }

    const AutoRewindPool auto_rewind(*tpool);

    char *node_name = p_strndup(tpool, payload, length);
    char *port_string = node_name + (colon - payload);
    *port_string++ = 0;

    const lb_node_config *node = instance->config->FindNode(node_name);
    if (node == nullptr) {
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
                          node->address, node->address.GetSize(),
                          port);

    char buffer[64];
    socket_address_to_string(buffer, sizeof(buffer), with_port,
                             node->address.GetSize());
    daemon_log(4, "enabling node %s (%s)\n", node_name, buffer);

    failure_unset({with_port, node->address.GetSize()}, FAILURE_OK);
}

static void
fade_node(const struct lb_instance *instance,
          const char *payload, size_t length)
{
    const char *colon = (const char *)memchr(payload, ':', length);
    if (colon == nullptr || colon == payload || colon == payload + length - 1) {
        daemon_log(3, "malformed FADE_NODE control packet: no port\n");
        return;
    }

    const AutoRewindPool auto_rewind(*tpool);

    char *node_name = p_strndup(tpool, payload, length);
    char *port_string = node_name + (colon - payload);
    *port_string++ = 0;

    const lb_node_config *node = instance->config->FindNode(node_name);
    if (node == nullptr) {
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
                          node->address, node->address.GetSize(),
                          port);

    char buffer[64];
    socket_address_to_string(buffer, sizeof(buffer), with_port,
                             node->address.GetSize());
    daemon_log(4, "fading node %s (%s)\n", node_name, buffer);

    /* set status "FADE" for 3 hours */
    failure_set({with_port, node->address.GetSize()}, FAILURE_FADE, 3 * 3600);
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
node_status_response(ControlServer *server, struct pool *pool,
                     SocketAddress address,
                     const char *payload, size_t length, const char *status,
                     GError **error_r)
{
    size_t status_length = strlen(status);

    size_t response_length = length + 1 + status_length;
    char *response = PoolAlloc<char>(*tpool, response_length);
    memcpy(response, payload, length);
    response[length] = 0;
    memcpy(response + length + 1, status, status_length);

    return server->Reply(pool, address,
                         CONTROL_NODE_STATUS, response, response_length,
                         error_r);
}

static void
query_node_status(LbControl *control,
                  const char *payload, size_t length,
                  SocketAddress address)
{
    if (address.GetSize() == 0) {
        daemon_log(3, "got NODE_STATUS from unbound client socket\n");
        return;
    }

    const char *colon = (const char *)memchr(payload, ':', length);
    if (colon == nullptr || colon == payload || colon == payload + length - 1) {
        node_status_response(control->server, tpool, address,
                             payload, length, "malformed", nullptr);
        daemon_log(3, "malformed NODE_STATUS control packet: no port\n");
        return;
    }

    const AutoRewindPool auto_rewind(*tpool);

    char *node_name = p_strndup(tpool, payload, length);
    char *port_string = node_name + (colon - payload);
    *port_string++ = 0;

    const lb_node_config *node =
        control->instance.config->FindNode(node_name);
    if (node == nullptr) {
        node_status_response(control->server, tpool, address,
                             payload, length, "unknown", nullptr);
        daemon_log(3, "unknown node in NODE_STATUS control packet\n");
        return;
    }

    char *endptr;
    unsigned port = strtoul(port_string, &endptr, 10);
    if (port == 0 || *endptr != 0) {
        node_status_response(control->server, tpool, address,
                             payload, length, "malformed", nullptr);
        daemon_log(3, "malformed NODE_STATUS control packet: port is not a number\n");
        return;
    }

    const struct sockaddr *with_port =
        sockaddr_set_port(tpool,
                          node->address, node->address.GetSize(),
                          port);

    char buffer[64];
    socket_address_to_string(buffer, sizeof(buffer), with_port,
                             node->address.GetSize());

    enum failure_status status =
        failure_get_status({with_port, node->address.GetSize()});
    const char *s = failure_status_to_string(status);

    GError *error = nullptr;
    if (!node_status_response(control->server, tpool, address,
                              payload, length, s,
                              &error)) {
        daemon_log(3, "%s\n", error->message);
        g_error_free(error);
    }
}

static void
query_stats(LbControl *control, SocketAddress address)
{
    struct beng_control_stats stats;
    lb_get_stats(&control->instance, &stats);

    const AutoRewindPool auto_rewind(*tpool);

    GError *error = nullptr;
    if (!control->server->Reply(tpool,
                                address,
                                CONTROL_STATS, &stats, sizeof(stats),
                                &error)) {
        daemon_log(3, "%s\n", error->message);
        g_error_free(error);
    }
}

static void
lb_control_packet(enum beng_control_command command,
                  const void *payload, size_t payload_length,
                  SocketAddress address,
                  void *ctx)
{
    auto *control = (LbControl *)ctx;

    switch (command) {
    case CONTROL_NOP:
    case CONTROL_TCACHE_INVALIDATE:
    case CONTROL_FADE_CHILDREN:
        break;

    case CONTROL_ENABLE_NODE:
        enable_node(&control->instance, (const char *)payload, payload_length);
        break;

    case CONTROL_FADE_NODE:
        fade_node(&control->instance, (const char *)payload, payload_length);
        break;

    case CONTROL_NODE_STATUS:
        query_node_status(control, (const char *)payload, payload_length,
                          address);
        break;

    case CONTROL_DUMP_POOLS:
        pool_dump_tree(control->instance.pool);
        break;

    case CONTROL_STATS:
        query_stats(control, address);
        break;

    case CONTROL_VERBOSE:
        if (payload_length == 1)
            daemon_log_config.verbose = *(const uint8_t *)payload;
        break;
    }
}

static void
lb_control_error(GError *error, gcc_unused void *ctx)
{
    daemon_log(2, "%s\n", error->message);
    g_error_free(error);
}

static constexpr struct control_handler lb_control_handler = {
    nullptr,
    lb_control_packet,
    lb_control_error,
};

bool
LbControl::Open(const struct lb_control_config &config, GError **error_r)
{
    assert(server == nullptr);

    server = control_server_new(config.bind_address,
                                &lb_control_handler, this,
                                error_r);
    return server != nullptr;
}

LbControl::~LbControl()
{
    delete server;
}

void
LbControl::Enable()
{
    server->Enable();
}

void
LbControl::Disable()
{
    server->Disable();
}
