/*
 * Hash table of monitors.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_hmonitor.hxx"
#include "lb_monitor.hxx"
#include "lb_ping_monitor.hxx"
#include "lb_syn_monitor.hxx"
#include "lb_expect_monitor.hxx"
#include "lb_config.hxx"
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
LbMonitorMap::Key::ToString(struct pool &pool) const
{
    return p_sprintf(&pool, "%s:[%s]:%u", monitor_name, node_name, port);
}

LbMonitorMap::LbMonitorMap(struct pool &_pool)
    :pool(pool_new_linear(&_pool, "LbMonitorMap", 4096))
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
    for (auto i : map)
        lb_monitor_enable(i.second);
}

void
LbMonitorMap::Add(const LbNodeConfig &node, unsigned port,
                  const LbMonitorConfig &config)
{
    const struct lb_monitor_class *class_ = nullptr;
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

    const Key key{config.name.c_str(), node.name.c_str(), port};
    auto r = map.insert(std::make_pair(key, nullptr));
    if (r.second) {
        /* doesn't exist yet: create it */
        struct pool *_pool = pool_new_linear(pool, "monitor", 1024);

        AllocatedSocketAddress address = node.address;
        if (port > 0)
            address.SetPort(port);

        r.first->second =
            lb_monitor_new(_pool, key.ToString(*_pool), &config,
                           SocketAddress(address,
                                         node.address.GetSize()),
                           class_);
        pool_unref(_pool);
    }
}

void
LbMonitorMap::Clear()
{
    for (auto i : map)
        lb_monitor_free(i.second);

    map.clear();
}
