/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Control.hxx"
#include "Instance.hxx"
#include "Config.hxx"
#include "lb_stats.hxx"
#include "control_server.hxx"
#include "failure.hxx"
#include "tpool.hxx"
#include "pool.hxx"
#include "translation/InvalidateParser.hxx"
#include "net/ToString.hxx"
#include "util/Exception.hxx"

#include <string.h>
#include <stdlib.h>

LbControl::LbControl(LbInstance &_instance)
    :logger("control"), instance(_instance) {}

inline void
LbControl::InvalidateTranslationCache(const void *payload,
                                      size_t payload_length)
{
    if (payload_length == 0) {
        /* flush the translation cache if the payload is empty */
        instance.FlushTranslationCaches();
        return;
    }

    const AutoRewindPool auto_rewind(*tpool);

    TranslationInvalidateRequest request;

    try {
        request = ParseTranslationInvalidateRequest(*tpool,
                                                    payload, payload_length);
    } catch (...) {
        logger(2, "malformed TCACHE_INVALIDATE control packet: ",
               GetFullMessage(std::current_exception()));
        return;
    }

    instance.InvalidateTranslationCaches(request);
}

inline void
LbControl::EnableNode(const char *payload, size_t length)
{
    const char *colon = (const char *)memchr(payload, ':', length);
    if (colon == nullptr || colon == payload || colon == payload + length - 1) {
        logger(3, "malformed FADE_NODE control packet: no port");
        return;
    }

    const AutoRewindPool auto_rewind(*tpool);

    char *node_name = p_strndup(tpool, payload, length);
    char *port_string = node_name + (colon - payload);
    *port_string++ = 0;

    const auto *node = instance.config.FindNode(node_name);
    if (node == nullptr) {
        logger(3, "unknown node in FADE_NODE control packet");
        return;
    }

    char *endptr;
    unsigned port = strtoul(port_string, &endptr, 10);
    if (port == 0 || *endptr != 0) {
        logger(3, "malformed FADE_NODE control packet: port is not a number");
        return;
    }

    const auto with_port = node->address.WithPort(port);

    char buffer[64];
    ToString(buffer, sizeof(buffer), with_port);
    logger(4, "enabling node ", node_name, " (", buffer, ")");

    failure_unset(with_port, FAILURE_OK);
}

inline void
LbControl::FadeNode(const char *payload, size_t length)
{
    const char *colon = (const char *)memchr(payload, ':', length);
    if (colon == nullptr || colon == payload || colon == payload + length - 1) {
        logger(3, "malformed FADE_NODE control packet: no port");
        return;
    }

    const AutoRewindPool auto_rewind(*tpool);

    char *node_name = p_strndup(tpool, payload, length);
    char *port_string = node_name + (colon - payload);
    *port_string++ = 0;

    const auto *node = instance.config.FindNode(node_name);
    if (node == nullptr) {
        logger(3, "unknown node in FADE_NODE control packet");
        return;
    }

    char *endptr;
    unsigned port = strtoul(port_string, &endptr, 10);
    if (port == 0 || *endptr != 0) {
        logger(3, "malformed FADE_NODE control packet: port is not a number");
        return;
    }

    const auto with_port = node->address.WithPort(port);

    char buffer[64];
    ToString(buffer, sizeof(buffer), with_port);
    logger(4, "fading node ", node_name, " (", buffer, ")");

    /* set status "FADE" for 3 hours */
    failure_set(with_port, FAILURE_FADE,
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

inline void
LbControl::QueryNodeStatus(ControlServer &control_server,
                           const char *payload, size_t length,
                           SocketAddress address)
try {
    if (address.GetSize() == 0) {
        logger(3, "got NODE_STATUS from unbound client socket");
        return;
    }

    const char *colon = (const char *)memchr(payload, ':', length);
    if (colon == nullptr || colon == payload || colon == payload + length - 1) {
        logger(3, "malformed NODE_STATUS control packet: no port");
        node_status_response(&control_server, address,
                             payload, length, "malformed");
        return;
    }

    const AutoRewindPool auto_rewind(*tpool);

    char *node_name = p_strndup(tpool, payload, length);
    char *port_string = node_name + (colon - payload);
    *port_string++ = 0;

    const auto *node = instance.config.FindNode(node_name);
    if (node == nullptr) {
        logger(3, "unknown node in NODE_STATUS control packet");
        node_status_response(&control_server, address,
                             payload, length, "unknown");
        return;
    }

    char *endptr;
    unsigned port = strtoul(port_string, &endptr, 10);
    if (port == 0 || *endptr != 0) {
        logger(3, "malformed NODE_STATUS control packet: port is not a number");
        node_status_response(&control_server, address,
                             payload, length, "malformed");
        return;
    }

    const auto with_port = node->address.WithPort(port);

    char buffer[64];
    ToString(buffer, sizeof(buffer), with_port);

    enum failure_status status = failure_get_status(with_port);
    const char *s = failure_status_to_string(status);

    node_status_response(&control_server, address,
                         payload, length, s);
} catch (...) {
    logger(3, std::current_exception());
}

inline void
LbControl::QueryStats(ControlServer &control_server,
                      SocketAddress address)
try {
    struct beng_control_stats stats;
    lb_get_stats(&instance, &stats);

    control_server.Reply(address,
                         CONTROL_STATS, &stats, sizeof(stats));
} catch (...) {
    logger(3, std::current_exception());
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
        break;

    case CONTROL_TCACHE_INVALIDATE:
        InvalidateTranslationCache(payload, payload_length);
        break;

    case CONTROL_FADE_CHILDREN:
        break;

    case CONTROL_ENABLE_NODE:
        if (is_privileged)
            EnableNode((const char *)payload, payload_length);
        break;

    case CONTROL_FADE_NODE:
        if (is_privileged)
            FadeNode((const char *)payload, payload_length);
        break;

    case CONTROL_NODE_STATUS:
        QueryNodeStatus(control_server,
                        (const char *)payload, payload_length,
                        address);
        break;

    case CONTROL_DUMP_POOLS:
        if (is_privileged)
            pool_dump_tree(instance.root_pool);
        break;

    case CONTROL_STATS:
        QueryStats(control_server, address);
        break;

    case CONTROL_VERBOSE:
        if (is_privileged && payload_length == 1) {
            SetLogLevel(*(const uint8_t *)payload);
        }

        break;
    }
}

void
LbControl::OnControlError(std::exception_ptr ep)
{
    logger(2, ep);
}

void
LbControl::Open(const LbControlConfig &config)
{
    assert(server == nullptr);

    server = std::make_unique<ControlServer>(instance.event_loop,
                                             *(ControlHandler *)this,
                                             config);
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
