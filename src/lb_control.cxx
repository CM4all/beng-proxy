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
#include "failure.hxx"
#include "tpool.hxx"
#include "pool.hxx"
#include "util/Exception.hxx"

#include <daemon/log.h>
#include <socket/address.h>

#include <string.h>
#include <stdlib.h>

LbControl::LbControl(LbInstance &_instance)
    :instance(_instance) {}

static void
enable_node(const LbInstance *instance,
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

    const auto *node = instance->config.FindNode(node_name);
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

    const auto with_port = node->address.WithPort(port);

    char buffer[64];
    socket_address_to_string(buffer, sizeof(buffer), with_port,
                             node->address.GetSize());
    daemon_log(4, "enabling node %s (%s)\n", node_name, buffer);

    failure_unset({with_port, node->address.GetSize()}, FAILURE_OK);
}

static void
fade_node(const LbInstance *instance,
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

    const auto *node = instance->config.FindNode(node_name);
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

    const auto with_port = node->address.WithPort(port);

    char buffer[64];
    socket_address_to_string(buffer, sizeof(buffer), with_port,
                             node->address.GetSize());
    daemon_log(4, "fading node %s (%s)\n", node_name, buffer);

    /* set status "FADE" for 3 hours */
    failure_set({with_port, node->address.GetSize()}, FAILURE_FADE,
                std::chrono::hours(3));
}

gcc_const
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

static void
node_status_response(ControlServer *server,
                     SocketAddress address,
                     const char *payload, size_t length, const char *status)
{
    size_t status_length = strlen(status);

    size_t response_length = length + 1 + status_length;
    char *response = PoolAlloc<char>(*tpool, response_length);
    memcpy(response, payload, length);
    response[length] = 0;
    memcpy(response + length + 1, status, status_length);

    server->Reply(address,
                  CONTROL_NODE_STATUS, response, response_length);
}

static void
query_node_status(LbControl *control, ControlServer &control_server,
                  const char *payload, size_t length,
                  SocketAddress address)
try {
    if (address.GetSize() == 0) {
        daemon_log(3, "got NODE_STATUS from unbound client socket\n");
        return;
    }

    const char *colon = (const char *)memchr(payload, ':', length);
    if (colon == nullptr || colon == payload || colon == payload + length - 1) {
        daemon_log(3, "malformed NODE_STATUS control packet: no port\n");
        node_status_response(control->server.get(), address,
                             payload, length, "malformed");
        return;
    }

    const AutoRewindPool auto_rewind(*tpool);

    char *node_name = p_strndup(tpool, payload, length);
    char *port_string = node_name + (colon - payload);
    *port_string++ = 0;

    const auto *node = control->instance.config.FindNode(node_name);
    if (node == nullptr) {
        daemon_log(3, "unknown node in NODE_STATUS control packet\n");
        node_status_response(control->server.get(), address,
                             payload, length, "unknown");
        return;
    }

    char *endptr;
    unsigned port = strtoul(port_string, &endptr, 10);
    if (port == 0 || *endptr != 0) {
        daemon_log(3, "malformed NODE_STATUS control packet: port is not a number\n");
        node_status_response(control->server.get(), address,
                             payload, length, "malformed");
        return;
    }

    const auto with_port = node->address.WithPort(port);

    char buffer[64];
    socket_address_to_string(buffer, sizeof(buffer), with_port,
                             node->address.GetSize());

    enum failure_status status =
        failure_get_status({with_port, node->address.GetSize()});
    const char *s = failure_status_to_string(status);

    node_status_response(&control_server, address,
                         payload, length, s);
} catch (const std::runtime_error &e) {
    daemon_log(3, "%s\n", e.what());
}

static void
query_stats(LbControl *control, ControlServer &control_server,
            SocketAddress address)
try {
    struct beng_control_stats stats;
    lb_get_stats(&control->instance, &stats);

    control_server.Reply(address,
                         CONTROL_STATS, &stats, sizeof(stats));
} catch (const std::runtime_error &e) {
    daemon_log(3, "%s\n", e.what());
}

void
LbControl::OnControlPacket(ControlServer &control_server,
                           enum beng_control_command command,
                           const void *payload, size_t payload_length,
                           SocketAddress address)
{
    /* only local clients are allowed to use most commands */
    const bool is_privileged = address.GetFamily() == AF_LOCAL;

    switch (command) {
    case CONTROL_NOP:
    case CONTROL_TCACHE_INVALIDATE:
    case CONTROL_FADE_CHILDREN:
        break;

    case CONTROL_ENABLE_NODE:
        if (is_privileged)
            enable_node(&instance, (const char *)payload, payload_length);
        break;

    case CONTROL_FADE_NODE:
        if (is_privileged)
            fade_node(&instance, (const char *)payload, payload_length);
        break;

    case CONTROL_NODE_STATUS:
        query_node_status(this, control_server,
                          (const char *)payload, payload_length,
                          address);
        break;

    case CONTROL_DUMP_POOLS:
        if (is_privileged)
            pool_dump_tree(instance.root_pool);
        break;

    case CONTROL_STATS:
        query_stats(this, control_server, address);
        break;

    case CONTROL_VERBOSE:
        if (is_privileged && payload_length == 1)
            daemon_log_config.verbose = *(const uint8_t *)payload;
        break;
    }
}

void
LbControl::OnControlError(std::exception_ptr ep)
{
    daemon_log(2, "%s\n", GetFullMessage(ep).c_str());
}

void
LbControl::Open(const LbControlConfig &config)
{
    assert(server == nullptr);

    std::unique_ptr<ControlServer> new_server(new ControlServer(*this));
    new_server->Open(instance.event_loop, config.bind_address);
    server = std::move(new_server);
}

LbControl::~LbControl()
{
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
