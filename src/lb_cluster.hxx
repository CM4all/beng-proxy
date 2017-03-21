/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_CLUSTER_HXX
#define BENG_LB_CLUSTER_HXX

#include "StickyHash.hxx"
#include "avahi/ConnectionListener.hxx"
#include "net/AllocatedSocketAddress.hxx"

#include <avahi-client/lookup.h>

#include <map>
#include <vector>
#include <string>

struct LbConfig;
struct LbGotoIfConfig;
struct LbBranchConfig;
struct LbGoto;
struct LbListenerConfig;
struct LbClusterConfig;
class MyAvahiClient;
class StickyCache;

class LbCluster final : AvahiConnectionListener {
    const LbClusterConfig &config;
    MyAvahiClient &avahi_client;
    AvahiServiceBrowser *avahi_browser = nullptr;

    StickyCache *sticky_cache = nullptr;

    class Member {
        LbCluster &cluster;

        AvahiServiceResolver *resolver = nullptr;

        AllocatedSocketAddress address;

    public:
        explicit Member(LbCluster &_cluster)
            :cluster(_cluster) {}
        ~Member();

        Member(const Member &) = delete;
        Member &operator=(const Member &) = delete;

        bool IsActive() const {
            return address.IsDefined();
        }

        bool HasFailed() const {
            return resolver == nullptr && !IsActive();
        }

        SocketAddress GetAddress() const {
            return address;
        }

        void Resolve(AvahiClient *client, AvahiIfIndex interface,
                     AvahiProtocol protocol,
                     const char *name,
                     const char *type,
                     const char *domain);
        void CancelResolve();

    private:
        void ServiceResolverCallback(AvahiIfIndex interface,
                                     AvahiResolverEvent event,
                                     const AvahiAddress *a,
                                     uint16_t port);
        static void ServiceResolverCallback(AvahiServiceResolver *r,
                                            AvahiIfIndex interface,
                                            AvahiProtocol protocol,
                                            AvahiResolverEvent event,
                                            const char *name,
                                            const char *type,
                                            const char *domain,
                                            const char *host_name,
                                            const AvahiAddress *a,
                                            uint16_t port,
                                            AvahiStringList *txt,
                                            AvahiLookupResultFlags flags,
                                            void *userdata);
    };

    typedef std::map<std::string, Member> MemberMap;
    MemberMap members;

    std::vector<const MemberMap::value_type *> active_members;

    bool dirty = false;

    unsigned last_pick = 0;

public:
    LbCluster(const LbClusterConfig &_config,
              MyAvahiClient &_avahi_client);
    ~LbCluster();

    std::pair<const char *, SocketAddress> Pick(sticky_hash_t sticky_hash);

private:
    void FillActive();

    void ServiceBrowserCallback(AvahiServiceBrowser *b,
                                AvahiIfIndex interface,
                                AvahiProtocol protocol,
                                AvahiBrowserEvent event,
                                const char *name,
                                const char *type,
                                const char *domain,
                                AvahiLookupResultFlags flags);
    static void ServiceBrowserCallback(AvahiServiceBrowser *b,
                                       AvahiIfIndex interface,
                                       AvahiProtocol protocol,
                                       AvahiBrowserEvent event,
                                       const char *name,
                                       const char *type,
                                       const char *domain,
                                       AvahiLookupResultFlags flags,
                                       void *userdata);

    /* virtual methods from class AvahiConnectionListener */
    void OnAvahiConnect(AvahiClient *client) override;
    void OnAvahiDisconnect() override;
};

class LbClusterMap {
    std::map<std::string, LbCluster> clusters;

public:
    void Scan(const LbConfig &config, MyAvahiClient &avahi_client);

    LbCluster *Find(const std::string &name) {
        auto i = clusters.find(name);
        return i != clusters.end()
            ? &i->second
            : nullptr;
    }

private:
    void Scan(const LbGotoIfConfig &config, MyAvahiClient &avahi_client);
    void Scan(const LbBranchConfig &config, MyAvahiClient &avahi_client);
    void Scan(const LbGoto &g, MyAvahiClient &avahi_client);
    void Scan(const LbListenerConfig &config, MyAvahiClient &avahi_client);

    void Scan(const LbClusterConfig &config, MyAvahiClient &avahi_client);
};

#endif
