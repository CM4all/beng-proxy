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
#include "address_edit.h"
#include "net/SocketAddress.hxx"

#include <map>

#include <string.h>

struct LBMonitorKey {
    const char *monitor_name;
    const char *node_name;
    unsigned port;

    gcc_pure
    bool operator<(const LBMonitorKey &other) const {
        auto r = strcmp(monitor_name, other.monitor_name);
        if (r != 0)
            return r < 0;

        r = strcmp(node_name, other.node_name);
        if (r != 0)
            return r < 0;

        return port < other.port;
    }

    char *ToString(struct pool &pool) const {
        return p_sprintf(&pool, "%s:[%s]:%u", monitor_name, node_name, port);
    }
};

static struct pool *hmonitor_pool;
static std::map<LBMonitorKey, LBMonitor *> hmonitor_map;

void
lb_hmonitor_init(struct pool *pool)
{
    hmonitor_pool = pool_new_linear(pool, "hmonitor", 4096);
}

void
lb_hmonitor_deinit(void)
{
    for (auto i : hmonitor_map)
        lb_monitor_free(i.second);

    hmonitor_map.clear();

    pool_unref(hmonitor_pool);
}

void
lb_hmonitor_enable(void)
{
    for (auto i : hmonitor_map)
        lb_monitor_enable(i.second);
}

void
lb_hmonitor_add(const LbNodeConfig *node, unsigned port,
                const LbMonitorConfig *config)
{
    const struct lb_monitor_class *class_ = nullptr;
    switch (config->type) {
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

    const LBMonitorKey key{config->name.c_str(), node->name.c_str(), port};
    auto r = hmonitor_map.insert(std::make_pair(key, nullptr));
    if (r.second) {
        /* doesn't exist yet: create it */
        struct pool *pool = pool_new_linear(hmonitor_pool, "monitor", 1024);

        const struct sockaddr *address = node->address;
        if (port > 0)
            address = sockaddr_set_port(pool, address,
                                        node->address.GetSize(), port);

        r.first->second =
            lb_monitor_new(pool, key.ToString(*pool), config,
                           SocketAddress(address,
                                         node->address.GetSize()),
                           class_);
        pool_unref(pool);
    }
}
