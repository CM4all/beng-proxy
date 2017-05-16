/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ClusterMap.hxx"
#include "lb_config.hxx"

void
LbClusterMap::Scan(const LbConfig &config, MyAvahiClient &avahi_client)
{
    for (const auto &i : config.listeners)
        Scan(i, avahi_client);
}

void
LbClusterMap::Scan(const LbGotoIfConfig &config, MyAvahiClient &avahi_client)
{
    Scan(config.destination, avahi_client);
}

void
LbClusterMap::Scan(const LbBranchConfig &config, MyAvahiClient &avahi_client)
{
    Scan(config.fallback, avahi_client);

    for (const auto &i : config.conditions)
        Scan(i, avahi_client);
}

void
LbClusterMap::Scan(const LbGoto &g, MyAvahiClient &avahi_client)
{
    if (g.cluster != nullptr)
        Scan(*g.cluster, avahi_client);

    if (g.branch != nullptr)
        Scan(*g.branch, avahi_client);
}

void
LbClusterMap::Scan(const LbListenerConfig &config, MyAvahiClient &avahi_client)
{
    Scan(config.destination, avahi_client);
}

void
LbClusterMap::Scan(const LbClusterConfig &config, MyAvahiClient &avahi_client)
{
    if (!config.HasZeroConf())
        /* doesn't need runtime data */
        return;

    auto i = clusters.find(config.name);
    if (i != clusters.end())
        /* already added */
        return;

    clusters.emplace(std::piecewise_construct,
                     std::forward_as_tuple(config.name),
                     std::forward_as_tuple(config, avahi_client));
}
