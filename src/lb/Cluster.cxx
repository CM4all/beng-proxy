/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Cluster.hxx"
#include "ClusterConfig.hxx"
#include "avahi/Client.hxx"
#include "StickyCache.hxx"
#include "failure.hxx"
#include "net/IPv4Address.hxx"
#include "net/IPv6Address.hxx"
#include "net/ToString.hxx"
#include "util/HashRing.hxx"
#include "util/ConstBuffer.hxx"

#include <avahi-common/error.h>
#include <sodium/crypto_generichash.h>

class LbCluster::StickyRing final : public HashRing<const MemberMap::value_type *,
                                                    sticky_hash_t,
                                                    4096, 8> {};

LbCluster::Member::~Member()
{
    if (resolver != nullptr)
        avahi_service_resolver_free(resolver);
}

const char *
LbCluster::Member::GetLogName(const char *key) const noexcept
{
    if (log_name.empty()) {
        if (address.IsNull())
            return key;

        log_name = key;

        char buffer[512];
        if (ToString(buffer, sizeof(buffer), address)) {
            log_name += " (";
            log_name += buffer;
            log_name += ")";
        }
    }

    return log_name.c_str();
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
                                          /* workaround: the following
                                             should be
                                             AVAHI_PROTO_UNSPEC
                                             (because we can deal with
                                             either protocol), but
                                             then avahi-daemon
                                             sometimes returns IPv6
                                             addresses from the cache,
                                             even though the service
                                             was registered as IPv4
                                             only */
                                          protocol,
                                          AvahiLookupFlags(0),
                                          ServiceResolverCallback, this);
    if (resolver == nullptr)
         cluster.logger(2, "Failed to create Avahi service resolver: ",
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
    return AllocatedSocketAddress(IPv4Address({src.address}, port));
}

static AllocatedSocketAddress
Import(AvahiIfIndex interface, const AvahiIPv6Address &src, unsigned port)
{
    struct in6_addr address;
    static_assert(sizeof(src.address) == sizeof(address), "Wrong size");
    std::copy_n(src.address, sizeof(src.address), address.s6_addr);
    return AllocatedSocketAddress(IPv6Address(address, port, interface));
}

static AllocatedSocketAddress
Import(AvahiIfIndex interface, const AvahiAddress &src, unsigned port)
{
    switch (src.proto) {
    case AVAHI_PROTO_INET:
        return Import(src.data.ipv4, port);

    case AVAHI_PROTO_INET6:
        return Import(interface, src.data.ipv6, port);
    }

    return AllocatedSocketAddress();
}

void
LbCluster::Member::ServiceResolverCallback(AvahiIfIndex interface,
                                           AvahiResolverEvent event,
                                           const AvahiAddress *a,
                                           uint16_t port)
{
    switch (event) {
    case AVAHI_RESOLVER_FOUND:
        address = Import(interface, *a, port);
        cluster.dirty = true;
        break;

    case AVAHI_RESOLVER_FAILURE:
        break;
    }

    CancelResolve();
}

void
LbCluster::Member::ServiceResolverCallback(AvahiServiceResolver *,
                                           AvahiIfIndex interface,
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
    member.ServiceResolverCallback(interface, event, a, port);
}

LbCluster::LbCluster(const LbClusterConfig &_config,
                     MyAvahiClient &_avahi_client)
    :config(_config),
     logger("cluster " + config.name),
     avahi_client(_avahi_client)
{
    if (config.HasZeroConf()) {
        avahi_client.AddListener(*this);
        avahi_client.Enable();
    }
}

LbCluster::~LbCluster()
{
    delete sticky_cache;
    delete sticky_ring;

    if (avahi_browser != nullptr)
        avahi_service_browser_free(avahi_browser);

    if (config.HasZeroConf())
        avahi_client.RemoveListener(*this);
}

const LbCluster::MemberMap::value_type &
LbCluster::PickNextZeroconf()
{
    assert(!active_members.empty());

    ++last_pick;
    if (last_pick >= active_members.size())
        last_pick = 0;

    return *active_members[last_pick];
}

const LbCluster::MemberMap::value_type &
LbCluster::PickNextGoodZeroconf()
{
    assert(!active_members.empty());

    unsigned remaining = active_members.size();

    while (true) {
        const auto &m = PickNextZeroconf();
        if (--remaining == 0 ||
            failure_get_status(m.second.GetAddress()) == FAILURE_OK)
            return m;
    }
}

std::pair<const char *, SocketAddress>
LbCluster::Pick(sticky_hash_t sticky_hash)
{
    if (dirty) {
        dirty = false;
        FillActive();
    }

    if (active_members.empty())
        return std::make_pair(nullptr, nullptr);

    if (sticky_hash != 0 && config.sticky_cache) {
        /* look up the sticky_hash in the StickyCache */

        assert(config.sticky_mode != StickyMode::NONE);

        if (sticky_cache == nullptr)
            /* lazy cache allocation */
            sticky_cache = new StickyCache();

        const auto *cached = sticky_cache->Get(sticky_hash);
        if (cached != nullptr) {
            /* cache hit */
            auto i = members.find(*cached);
            if (i != members.end() && i->second.IsActive() &&
                // TODO: allow FAILURE_FADE here?
                failure_get_status(i->second.GetAddress()) == FAILURE_OK)
                /* the node is active, we can use it */
                return std::make_pair(i->second.GetLogName(i->first.c_str()),
                                      i->second.GetAddress());

            sticky_cache->Remove(sticky_hash);
        }

        /* cache miss or cached node not active: fall back to
           round-robin and remember the new pick in the cache */
    } else if (sticky_hash != 0) {
        /* use consistent hashing */

        assert(sticky_ring != nullptr);

        auto *i = sticky_ring->Pick(sticky_hash);
        assert(i != nullptr);

        unsigned retries = active_members.size();
        while (true) {
            if (--retries == 0 ||
                failure_get_status(i->second.GetAddress()) == FAILURE_OK)
                return std::make_pair(i->second.GetLogName(i->first.c_str()),
                                      i->second.GetAddress());

            /* the node is known-bad; pick the next one in the ring */
            const auto next = sticky_ring->FindNext(sticky_hash);
            sticky_hash = next.first;
            i = next.second;
        }
    }

    const auto &i = PickNextGoodZeroconf();

    if (sticky_hash != 0)
        sticky_cache->Put(sticky_hash, i.first);

    return std::make_pair(i.second.GetLogName(i.first.c_str()),
                          i.second.GetAddress());
}

static void
UpdateHash(crypto_generichash_state &state, ConstBuffer<void> p)
{
    crypto_generichash_update(&state, (const unsigned char *)p.data, p.size);
}

static void
UpdateHash(crypto_generichash_state &state, SocketAddress address)
{
    assert(!address.IsNull());

    UpdateHash(state, address.GetSteadyPart());
}

template<typename T>
static void
UpdateHashT(crypto_generichash_state &state, const T &data)
{
    crypto_generichash_update(&state,
                              (const unsigned char *)&data, sizeof(data));
}

void
LbCluster::FillActive()
{
    active_members.clear();
    active_members.reserve(members.size());

    for (const auto &i : members)
        if (i.second.IsActive())
            active_members.push_back(&i);

    if (!config.sticky_cache) {
        if (sticky_ring == nullptr)
            /* lazy allocation */
            sticky_ring = new StickyRing();

        /**
         * Functor class which generates a #HashRing hash for a
         * cluster member combined with a replica number.
         */
        struct MemberHasher {
            gcc_pure
            sticky_hash_t operator()(const MemberMap::value_type *member,
                                     size_t replica) const {
                /* use libsodium's "generichash" (BLAKE2b) which is
                   secure enough for class HashRing */
                union {
                    unsigned char hash[crypto_generichash_BYTES];
                    sticky_hash_t result;
                } u;

                crypto_generichash_state state;
                crypto_generichash_init(&state, nullptr, 0, sizeof(u.hash));
                UpdateHash(state, member->second.GetAddress());
                UpdateHashT(state, replica);
                crypto_generichash_final(&state, u.hash, sizeof(u.hash));

                return u.result;
            }
        };

        sticky_ring->Build(active_members, MemberHasher());
    }
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
        auto i = members.find(MakeKey(interface, protocol, name,
                                      type, domain));
        if (i != members.end()) {
            /* purge this entry from the "failure" map, because it
               will never be used again anyway */
            failure_unset(i->second.GetAddress(), FAILURE_OK);

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
        logger(2, "Failed to create Avahi service browser: ",
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
