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
            return !address.IsNull();
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

    const LbClusterConfig &GetConfig() const {
        return config;
    }

    std::pair<const char *, SocketAddress> Pick(sticky_hash_t sticky_hash);

private:
    void FillActive();

    /**
     * Pick the next active Zeroconf member in a round-robin way.
     * Does not update the #StickyCache.
     */
    const MemberMap::value_type &PickNextZeroconf();

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

#endif
