/*
 * Copyright 2007-2019 Content Management AG
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
#include "pool/tpool.hxx"
#include "pool/pool.hxx"
#include "translation/InvalidateParser.hxx"
#include "net/ToString.hxx"
#include "net/FailureManager.hxx"
#include "util/Exception.hxx"

#include <systemd/sd-journal.h>

#include <string.h>
#include <stdlib.h>

using namespace BengProxy;

LbControl::LbControl(LbInstance &_instance, const LbControlConfig &config)
    :logger("control"), instance(_instance),
     server(instance.event_loop, *this, config)
{
}

inline void
LbControl::InvalidateTranslationCache(ConstBuffer<void> payload,
                                      SocketAddress address)
{
    if (payload.empty()) {
        /* flush the translation cache if the payload is empty */

        char address_buffer[256];
        sd_journal_send("MESSAGE=control TCACHE_INVALIDATE *",
                        "REMOTE_ADDR=%s",
                        ToString(address_buffer, sizeof(address_buffer),
                                 address, "?"),
                        "PRIORITY=%i", LOG_DEBUG,
                        nullptr);

        instance.FlushTranslationCaches();
        return;
    }

    const AutoRewindPool auto_rewind(*tpool);

    TranslationInvalidateRequest request;

    try {
        request = ParseTranslationInvalidateRequest(*tpool, payload.data,
                                                    payload.size);
    } catch (...) {
        logger(2, "malformed TCACHE_INVALIDATE control packet: ",
               GetFullMessage(std::current_exception()));
        return;
    }

    char address_buffer[256];
    sd_journal_send("MESSAGE=control TCACHE_INVALIDATE %s", request.ToString().c_str(),
                    "REMOTE_ADDR=%s",
                    ToString(address_buffer, sizeof(address_buffer),
                             address, "?"),
                    "PRIORITY=%i", LOG_DEBUG,
                    nullptr);

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
    logger(4, "enabling node ", node_name, " (",
           ToString(buffer, sizeof(buffer), with_port, "?"),
           ")");

    instance.failure_manager.Make(with_port).UnsetAll();
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
    logger(4, "fading node ", node_name, " (",
           ToString(buffer, sizeof(buffer), with_port, "?"),
           ")");

    /* set status "FADE" for 3 hours */
    instance.failure_manager.Make(with_port)
        .SetFade(GetEventLoop().SteadyNow(), std::chrono::hours(3));
}

gcc_const
static const char *
failure_status_to_string(FailureStatus status)
{
    switch (status) {
    case FailureStatus::OK:
        return "ok";

    case FailureStatus::FADE:
        return "fade";

    case FailureStatus::PROTOCOL:
    case FailureStatus::CONNECT:
    case FailureStatus::MONITOR:
        break;
    }

    return "error";
}

static void
node_status_response(ControlServer *server,
                     SocketAddress address,
                     StringView payload, const char *status)
{
    size_t status_length = strlen(status);

    size_t response_length = payload.size + 1 + status_length;
    char *response = PoolAlloc<char>(*tpool, response_length);
    memcpy(response, payload.data, payload.size);
    response[payload.size] = 0;
    memcpy(response + payload.size + 1, status, status_length);

    server->Reply(address,
                  ControlCommand::NODE_STATUS, response, response_length);
}

inline void
LbControl::QueryNodeStatus(ControlServer &control_server,
                           StringView payload,
                           SocketAddress address)
try {
    if (address.GetSize() == 0) {
        logger(3, "got NODE_STATUS from unbound client socket");
        return;
    }

    const char *colon = payload.Find(':');
    if (colon == nullptr || colon == payload.data || colon == payload.data + payload.size - 1) {
        logger(3, "malformed NODE_STATUS control packet: no port");
        node_status_response(&control_server, address,
                             payload, "malformed");
        return;
    }

    const AutoRewindPool auto_rewind(*tpool);

    char *node_name = p_strdup(*tpool, payload);
    char *port_string = node_name + (colon - payload.data);
    *port_string++ = 0;

    const auto *node = instance.config.FindNode(node_name);
    if (node == nullptr) {
        logger(3, "unknown node in NODE_STATUS control packet");
        node_status_response(&control_server, address,
                             payload, "unknown");
        return;
    }

    char *endptr;
    unsigned port = strtoul(port_string, &endptr, 10);
    if (port == 0 || *endptr != 0) {
        logger(3, "malformed NODE_STATUS control packet: port is not a number");
        node_status_response(&control_server, address,
                             payload, "malformed");
        return;
    }

    const auto with_port = node->address.WithPort(port);

    auto status = instance.failure_manager.Get(GetEventLoop().SteadyNow(),
                                               with_port);
    const char *s = failure_status_to_string(status);

    node_status_response(&control_server, address,
                         payload, s);
} catch (...) {
    logger(3, std::current_exception());
}

inline void
LbControl::QueryStats(ControlServer &control_server,
                      SocketAddress address)
try {
    const auto stats = instance.GetStats();
    control_server.Reply(address,
                         ControlCommand::STATS, &stats, sizeof(stats));
} catch (...) {
    logger(3, std::current_exception());
}

void
LbControl::OnControlPacket(ControlServer &control_server,
                           BengProxy::ControlCommand command,
                           ConstBuffer<void> payload,
                           SocketAddress address)
{
    /* only local clients are allowed to use most commands */
    const bool is_privileged = address.GetFamily() == AF_LOCAL;

    switch (command) {
    case ControlCommand::NOP:
        break;

    case ControlCommand::TCACHE_INVALIDATE:
        InvalidateTranslationCache(payload, address);
        break;

    case ControlCommand::FADE_CHILDREN:
        break;

    case ControlCommand::ENABLE_NODE:
        if (is_privileged)
            EnableNode((const char *)payload.data, payload.size);
        break;

    case ControlCommand::FADE_NODE:
        if (is_privileged)
            FadeNode((const char *)payload.data, payload.size);
        break;

    case ControlCommand::NODE_STATUS:
        QueryNodeStatus(control_server,
                        StringView(payload),
                        address);
        break;

    case ControlCommand::DUMP_POOLS:
        if (is_privileged)
            pool_dump_tree(instance.root_pool);
        break;

    case ControlCommand::STATS:
        QueryStats(control_server, address);
        break;

    case ControlCommand::VERBOSE:
        if (is_privileged && payload.size == 1) {
            SetLogLevel(*(const uint8_t *)payload.data);
        }

        break;

    case ControlCommand::DISABLE_ZEROCONF:
    case ControlCommand::ENABLE_ZEROCONF:
    case ControlCommand::FLUSH_NFS_CACHE:
    case ControlCommand::FLUSH_FILTER_CACHE:
        /* not applicable */
        break;
    }
}

void
LbControl::OnControlError(std::exception_ptr ep) noexcept
{
    logger(2, ep);
}
