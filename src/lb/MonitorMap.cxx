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

#include "MonitorMap.hxx"
#include "MonitorController.hxx"
#include "PingMonitor.hxx"
#include "SynMonitor.hxx"
#include "ExpectMonitor.hxx"
#include "MonitorConfig.hxx"
#include "ClusterConfig.hxx"
#include "pool.hxx"
#include "tpool.hxx"
#include "net/SocketAddress.hxx"

#include <map>

#include <string.h>

inline bool
LbMonitorMap::Key::operator<(const Key &other) const
{
    auto r = strcmp(monitor_name, other.monitor_name);
    if (r != 0)
        return r < 0;

    r = strcmp(node_name, other.node_name);
    if (r != 0)
        return r < 0;

    return port < other.port;
}

char *
LbMonitorMap::Key::ToString(struct pool &_pool) const
{
    return p_sprintf(&_pool, "%s:[%s]:%u", monitor_name, node_name, port);
}

LbMonitorMap::LbMonitorMap(struct pool &_pool, EventLoop &_event_loop,
                           FailureManager &_failure_manager)
    :pool(pool_new_linear(&_pool, "LbMonitorMap", 4096)),
     event_loop(_event_loop), failure_manager(_failure_manager)
{
}

LbMonitorMap::~LbMonitorMap()
{
    Clear();

    pool_unref(pool);
}

void
LbMonitorMap::Enable()
{
    for (auto &i : map)
        i.second->Enable();
}

void
LbMonitorMap::Add(const char *node_name, SocketAddress address,
                  const LbMonitorConfig &config)
{
    const LbMonitorClass *class_ = nullptr;
    switch (config.type) {
    case LbMonitorConfig::Type::NONE:
        /* nothing to do */
        return;

    case LbMonitorConfig::Type::PING:
        class_ = &ping_monitor_class;
        break;

    case LbMonitorConfig::Type::CONNECT:
        class_ = &syn_monitor_class;
        break;

    case LbMonitorConfig::Type::TCP_EXPECT:
        class_ = &expect_monitor_class;
        break;
    }

    assert(class_ != NULL);

    const AutoRewindPool auto_rewind(*tpool);

    const Key key{config.name.c_str(), node_name, address.GetPort()};
    auto r = map.insert(std::make_pair(key, nullptr));
    if (r.second) {
        /* doesn't exist yet: create it */
        struct pool *_pool = pool_new_linear(pool, "monitor", 1024);

        r.first->second = std::make_unique<LbMonitorController>(event_loop, failure_manager,
                                                                *_pool, key.ToString(*_pool), config,
                                                                address, *class_);
        pool_unref(_pool);
    }
}

void
LbMonitorMap::Add(const LbNodeConfig &node, unsigned port,
                  const LbMonitorConfig &config)
{
    AllocatedSocketAddress address = node.address;
    if (port > 0)
        address.SetPort(port);

    Add(node.name.c_str(), address, config);
}

void
LbMonitorMap::Clear()
{
    map.clear();
}
