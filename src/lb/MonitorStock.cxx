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

#include "MonitorStock.hxx"
#include "MonitorController.hxx"
#include "PingMonitor.hxx"
#include "SynMonitor.hxx"
#include "ExpectMonitor.hxx"
#include "MonitorConfig.hxx"
#include "ClusterConfig.hxx"
#include "net/SocketAddress.hxx"
#include "net/ToString.hxx"

gcc_const
static const LbMonitorClass &
LookupMonitorClass(LbMonitorConfig::Type type)
{
    switch (type) {
    case LbMonitorConfig::Type::NONE:
        assert(false);
        gcc_unreachable();

    case LbMonitorConfig::Type::PING:
        return ping_monitor_class;

    case LbMonitorConfig::Type::CONNECT:
        return syn_monitor_class;

    case LbMonitorConfig::Type::TCP_EXPECT:
        return expect_monitor_class;
    }

    gcc_unreachable();
}

static std::string
ToString(SocketAddress address)
{
    char buffer[4096];
    return ToString(buffer, sizeof(buffer), address)
        ? buffer
        : "unknown";
}

LbMonitorStock::LbMonitorStock(EventLoop &_event_loop,
                               FailureManager &_failure_manager,
                               const LbMonitorConfig &_config)
    :event_loop(_event_loop), failure_manager(_failure_manager),
     config(_config), class_(LookupMonitorClass(config.type))
{
}

LbMonitorStock::~LbMonitorStock()
{
}

void
LbMonitorStock::Add(const char *node_name, SocketAddress address)
{
    map.emplace(std::piecewise_construct,
                std::forward_as_tuple(ToString(address)),
                std::forward_as_tuple(event_loop, failure_manager,
                                      node_name,
                                      config, address, class_));
}

void
LbMonitorStock::Add(const LbNodeConfig &node, unsigned port)
{
    AllocatedSocketAddress address = node.address;
    if (port > 0)
        address.SetPort(port);

    Add(node.name.c_str(), address);
}
