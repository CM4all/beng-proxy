/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_cluster.hxx"
#include "lb_config.hxx"
#include "avahi/Client.hxx"

#include <daemon/log.h>

#include <avahi-common/error.h>

LbCluster::Member::~Member()
{
    if (resolver != nullptr)
        avahi_service_resolver_free(resolver);
}

void
LbCluster::Member::Resolve(AvahiClient *client, AvahiIfIndex interface,
                           AvahiProtocol protocol,
                           const char *name,
                           const char *type,
                           const char *domain)
{
    assert(resolver == nullptr);

    resolver = avahi_service_resolver_new(client, interface, protocol,
                                          name, type, domain,
                                          AVAHI_PROTO_UNSPEC,
                                          AvahiLookupFlags(0),
                                          ServiceResolverCallback, this);
    if (resolver == nullptr)
         daemon_log(2, "Failed to create Avahi service resolver: %s\n",
                   avahi_strerror(avahi_client_errno(client)));
}

void
LbCluster::Member::CancelResolve()
{
    if (resolver != nullptr) {
        avahi_service_resolver_free(resolver);
        resolver = nullptr;
    }
}

static AllocatedSocketAddress
Import(const AvahiIPv4Address &src, unsigned port)
{
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = src.address;
    memset(sin.sin_zero, 0, sizeof(sin.sin_zero));
    return AllocatedSocketAddress(SocketAddress((const struct sockaddr *)&sin,
                                                sizeof(sin)));
}

static AllocatedSocketAddress
Import(const AvahiIPv6Address &src, unsigned port)
{
    struct sockaddr_in6 sin;
    sin.sin6_family = AF_INET6;
    sin.sin6_flowinfo = 0;
    sin.sin6_port = htons(port);
    static_assert(sizeof(sin.sin6_addr) == sizeof(src), "");
    memcpy(&sin.sin6_addr, &src, sizeof(src));
    sin.sin6_scope_id = 0;
    return AllocatedSocketAddress(SocketAddress((const struct sockaddr *)&sin,
                                                sizeof(sin)));
}

static AllocatedSocketAddress
Import(const AvahiAddress &src, unsigned port)
{
    switch (src.proto) {
    case AVAHI_PROTO_INET:
        return Import(src.data.ipv4, port);

    case AVAHI_PROTO_INET6:
        return Import(src.data.ipv6, port);
    }

    return AllocatedSocketAddress();
}

void
LbCluster::Member::ServiceResolverCallback(AvahiResolverEvent event,
                                           const AvahiAddress *a,
                                           uint16_t port)
{
    switch (event) {
    case AVAHI_RESOLVER_FOUND:
        address = Import(*a, port);
        cluster.dirty = true;
        break;

    case AVAHI_RESOLVER_FAILURE:
        break;
    }

    CancelResolve();
}

void
LbCluster::Member::ServiceResolverCallback(AvahiServiceResolver *,
                                           gcc_unused AvahiIfIndex interface,
                                           gcc_unused AvahiProtocol protocol,
                                           AvahiResolverEvent event,
                                           gcc_unused const char *name,
                                           gcc_unused const char *type,
                                           gcc_unused const char *domain,
                                           gcc_unused const char *host_name,
                                           const AvahiAddress *a,
                                           uint16_t port,
                                           gcc_unused AvahiStringList *txt,
                                           gcc_unused AvahiLookupResultFlags flags,
                                           void *userdata)
{
    auto &member = *(LbCluster::Member *)userdata;
    member.ServiceResolverCallback(event, a, port);
}

LbCluster::LbCluster(const LbClusterConfig &_config,
                     MyAvahiClient &_avahi_client)
    :config(_config), avahi_client(_avahi_client)
{
    if (config.HasZeroConf()) {
        avahi_client.AddListener(*this);
        avahi_client.Enable();
    }
}

LbCluster::~LbCluster()
{
    if (avahi_browser != nullptr)
        avahi_service_browser_free(avahi_browser);

    if (config.HasZeroConf())
        avahi_client.RemoveListener(*this);
}

std::pair<const char *, SocketAddress>
LbCluster::Pick()
{
    if (dirty) {
        dirty = false;
        FillActive();
    }

    if (active_members.empty())
        return std::make_pair(nullptr, nullptr);

    ++last_pick;
    if (last_pick >= active_members.size())
        last_pick = 0;

    const auto &i = *active_members[last_pick];
    return std::make_pair(i.first.c_str(), i.second.GetAddress());
}

void
LbCluster::FillActive()
{
    active_members.clear();
    active_members.reserve(members.size());

    for (const auto &i : members)
        if (i.second.IsActive())
            active_members.push_back(&i);
}

static std::string
MakeKey(AvahiIfIndex interface,
        AvahiProtocol protocol,
        const char *name,
        const char *type,
        const char *domain)
{
    char buffer[2048];
    snprintf(buffer, sizeof(buffer), "%d/%d/%s/%s/%s",
             (int)interface, (int)protocol, name, type, domain);
    return buffer;
}

void
LbCluster::ServiceBrowserCallback(AvahiServiceBrowser *b,
                                  AvahiIfIndex interface,
                                  AvahiProtocol protocol,
                                  AvahiBrowserEvent event,
                                  const char *name,
                                  const char *type,
                                  const char *domain,
                                  gcc_unused AvahiLookupResultFlags flags)
{
    if (event == AVAHI_BROWSER_NEW) {
        auto i = members.emplace(std::piecewise_construct,
                                 std::forward_as_tuple(MakeKey(interface,
                                                               protocol, name,
                                                               type, domain)),
                                 std::forward_as_tuple(*this));
        if (i.second || i.first->second.HasFailed())
            i.first->second.Resolve(avahi_service_browser_get_client(b),
                                    interface, protocol,
                                    name, type, domain);
    } else if (event == AVAHI_BROWSER_REMOVE) {
        auto i = members.find(name);
        if (i != members.end()) {
            if (i->second.IsActive())
                dirty = true;
            members.erase(i);
        }
    }
}

void
LbCluster::ServiceBrowserCallback(AvahiServiceBrowser *b,
                                  AvahiIfIndex interface,
                                  AvahiProtocol protocol,
                                  AvahiBrowserEvent event,
                                  const char *name,
                                  const char *type,
                                  const char *domain,
                                  AvahiLookupResultFlags flags,
                                  void *userdata)
{
    auto &cluster = *(LbCluster *)userdata;
    cluster.ServiceBrowserCallback(b, interface, protocol, event, name,
                                   type, domain, flags);
}

void
LbCluster::OnAvahiConnect(AvahiClient *client)
{
    avahi_browser = avahi_service_browser_new(client, AVAHI_IF_UNSPEC,
                                              AVAHI_PROTO_UNSPEC,
                                              config.zeroconf_service.c_str(),
                                              config.zeroconf_domain.empty()
                                              ? nullptr
                                              : config.zeroconf_domain.c_str(),
                                              AvahiLookupFlags(0),
                                              ServiceBrowserCallback, this);
    if (avahi_browser == nullptr)
        daemon_log(2, "Failed to create Avahi service browser: %s\n",
                   avahi_strerror(avahi_client_errno(client)));
}

void
LbCluster::OnAvahiDisconnect()
{
    for (auto &i : members)
        i.second.CancelResolve();

    if (avahi_browser != nullptr) {
        avahi_service_browser_free(avahi_browser);
        avahi_browser = nullptr;
    }
}

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
