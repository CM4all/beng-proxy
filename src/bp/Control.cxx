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
#include "bp_stats.hxx"
#include "control_distribute.hxx"
#include "control_server.hxx"
#include "control_local.hxx"
#include "bp_instance.hxx"
#include "translation/Request.hxx"
#include "translation/Cache.hxx"
#include "translation/Protocol.hxx"
#include "translation/InvalidateParser.hxx"
#include "tpool.hxx"
#include "pool.hxx"
#include "net/UdpDistribute.hxx"
#include "net/SocketAddress.hxx"
#include "net/IPv4Address.hxx"
#include "io/Logger.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Exception.hxx"
#include "util/Macros.hxx"

#include <assert.h>
#include <string.h>

static void
control_tcache_invalidate(BpInstance *instance,
                          const void *payload, size_t payload_length)
{
    if (payload_length == 0) {
        /* flush the translation cache if the payload is empty */
        translate_cache_flush(*instance->translate_cache);
        return;
    }

    const AutoRewindPool auto_rewind(*tpool);

    TranslationInvalidateRequest request;

    try {
        request = ParseTranslationInvalidateRequest(*tpool,
                                                    payload, payload_length);
    } catch (...) {
        LogConcat(2, "control",
                  "malformed TCACHE_INVALIDATE control packet: ",
                  std::current_exception());
        return;
    }

    translate_cache_invalidate(*instance->translate_cache, request,
                               ConstBuffer<TranslationCommand>(request.commands.raw(),
                                                               request.commands.size()),
                               request.site);
}

static void
query_stats(BpInstance *instance, ControlServer *server,
            SocketAddress address)
{
    if (address.GetSize() == 0)
        /* TODO: this packet was forwarded by the master process, and
           has no source address; however, the master process must get
           statistics from all worker processes (even those that have
           exited already) */
        return;

    struct beng_control_stats stats;
    bp_get_stats(instance, &stats);

    try {
        server->Reply(address,
                      CONTROL_STATS, &stats, sizeof(stats));
    } catch (...) {
        LogConcat(3, "control", std::current_exception());
    }
}

static void
handle_control_packet(BpInstance *instance, ControlServer *server,
                      enum beng_control_command command,
                      const void *payload, size_t payload_length,
                      SocketAddress address)
{
    LogConcat(5, "control", "command=", int(command),
              " payload_length=", unsigned(payload_length));

    /* only local clients are allowed to use most commands */
    const bool is_privileged = address.GetFamily() == AF_LOCAL;

    switch (command) {
    case CONTROL_NOP:
        /* duh! */
        break;

    case CONTROL_TCACHE_INVALIDATE:
        control_tcache_invalidate(instance, payload, payload_length);
        break;

    case CONTROL_DUMP_POOLS:
        if (is_privileged)
            pool_dump_tree(instance->root_pool);
        break;

    case CONTROL_ENABLE_NODE:
    case CONTROL_FADE_NODE:
    case CONTROL_NODE_STATUS:
        /* only for beng-lb */
        break;

    case CONTROL_STATS:
        query_stats(instance, server, address);
        break;

    case CONTROL_VERBOSE:
        if (is_privileged && payload_length == 1)
            SetLogLevel(*(const uint8_t *)payload);
        break;

    case CONTROL_FADE_CHILDREN:
        if (payload_length > 0)
            /* tagged fade is allowed for any unprivileged client */
            instance->FadeTaggedChildren(std::string((const char *)payload,
                                                     payload_length).c_str());
        else if (is_privileged)
            /* unconditional fade is only allowed for privileged
               clients */
            instance->FadeChildren();
        break;
    }
}

void
BpInstance::OnControlPacket(ControlServer &_control_server,
                            enum beng_control_command command,
                            const void *payload, size_t payload_length,
                            SocketAddress address)
{
    handle_control_packet(this, &_control_server,
                          command, payload, payload_length,
                          address);
}

void
BpInstance::OnControlError(std::exception_ptr ep)
{
    LogConcat(2, "control", ep);
}

void
global_control_handler_init(BpInstance *instance)
{
    if (instance->config.control_listen.empty())
        return;

    instance->control_distribute = new ControlDistribute(instance->event_loop,
                                                         *instance);

    for (const auto &control_listen : instance->config.control_listen) {
        instance->control_servers.emplace_front(instance->event_loop,
                                                *instance->control_distribute,
                                                control_listen);
    }
}

void
global_control_handler_deinit(BpInstance *instance)
{
    instance->control_servers.clear();
    delete instance->control_distribute;
}

void
global_control_handler_enable(BpInstance &instance)
{
    for (auto &c : instance.control_servers)
        c.Enable();
}

void
global_control_handler_disable(BpInstance &instance)
{
    for (auto &c : instance.control_servers)
        c.Disable();
}

UniqueSocketDescriptor
global_control_handler_add_fd(BpInstance *instance)
{
    assert(!instance->control_servers.empty());
    assert(instance->control_distribute != nullptr);

    return instance->control_distribute->Add();
}

void
global_control_handler_set_fd(BpInstance *instance,
                              UniqueSocketDescriptor &&fd)
{
    assert(!instance->control_servers.empty());
    assert(instance->control_distribute != nullptr);

    instance->control_distribute->Clear();

    /* erase all but one */
    instance->control_servers.erase_after(instance->control_servers.begin(),
                                          instance->control_servers.end());

    /* replace the one */
    instance->control_servers.front().SetFd(std::move(fd));
}

/*
 * local (implicit) control channel
 */

void
local_control_handler_init(BpInstance *instance)
{
    instance->local_control_server =
        control_local_new("beng_control:pid=", *instance);
}

void
local_control_handler_deinit(BpInstance *instance)
{
    control_local_free(instance->local_control_server);
}

void
local_control_handler_open(BpInstance *instance)
{
    control_local_open(instance->local_control_server,
                       instance->event_loop);
}
