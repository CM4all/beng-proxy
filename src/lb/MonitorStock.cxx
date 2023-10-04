// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "MonitorStock.hxx"
#include "MonitorController.hxx"
#include "MonitorRef.hxx"
#include "PingMonitor.hxx"
#include "SynMonitor.hxx"
#include "ExpectMonitor.hxx"
#include "MonitorConfig.hxx"
#include "ClusterConfig.hxx"
#include "net/SocketAddress.hxx"
#include "net/ToString.hxx"

#include <assert.h>

[[gnu::const]]
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

LbMonitorStock::LbMonitorStock(EventLoop &_event_loop,
			       FailureManager &_failure_manager,
			       const LbMonitorConfig &_config)
	:event_loop(_event_loop), failure_manager(_failure_manager),
	 config(_config), class_(LookupMonitorClass(config.type))
{
}

LbMonitorStock::~LbMonitorStock()
{
	/* at this point, all LbMonitorController references
	   (LbMonitorRef) must be freed */
	assert(map.empty());
}

LbMonitorRef
LbMonitorStock::Add(const char *node_name, SocketAddress address)
{
	auto &m = map.emplace(std::piecewise_construct,
			      std::forward_as_tuple(ToString(address)),
			      std::forward_as_tuple(event_loop, failure_manager,
						    node_name,
						    config, address, class_))
		.first->second;
	return {*this, m};
}

LbMonitorRef
LbMonitorStock::Add(const LbNodeConfig &node, unsigned port)
{
	AllocatedSocketAddress address = node.address;
	if (port > 0)
		address.SetPort(port);

	return Add(node.name.c_str(), address);
}

void
LbMonitorStock::Remove(LbMonitorController &m) noexcept
{
	auto i = map.find(ToString(m.GetAddress()));
	assert(i != map.end());
	assert(&i->second == &m);
	map.erase(i);
}
