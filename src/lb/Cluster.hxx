/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_CLUSTER_HXX
#define BENG_LB_CLUSTER_HXX

#include "StickyHash.hxx"
#include "avahi/ExplorerListener.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "io/Logger.hxx"

#include <avahi-client/lookup.h>

#include <map>
#include <vector>
#include <string>
#include <memory>

struct LbConfig;
struct LbGotoIfConfig;
struct LbListenerConfig;
struct LbClusterConfig;
class MyAvahiClient;
class StickyCache;
class AvahiServiceExplorer;

class LbCluster final : AvahiServiceExplorerListener {
    const LbClusterConfig &config;

    const Logger logger;

    std::unique_ptr<AvahiServiceExplorer> explorer;

    class StickyRing;
    StickyRing *sticky_ring = nullptr;

    StickyCache *sticky_cache = nullptr;

    class Member {
        AllocatedSocketAddress address;

        mutable std::string log_name;

    public:
        explicit Member(SocketAddress _address)
            :address(_address) {}

        Member(const Member &) = delete;
        Member &operator=(const Member &) = delete;

        SocketAddress GetAddress() const {
            return address;
        }

        void SetAddress(SocketAddress _address) {
            address = _address;
        }

        /**
         * Obtain a name identifying this object for logging.
         */
        gcc_pure
        const char *GetLogName(const char *key) const noexcept;
    };

    typedef std::map<std::string, Member> MemberMap;
    MemberMap members;

    std::vector<const MemberMap::value_type *> active_members;

    bool dirty = false;

    unsigned last_pick = 0;

public:
    LbCluster(const LbClusterConfig &_config,
              MyAvahiClient &avahi_client);
    ~LbCluster();

    const LbClusterConfig &GetConfig() const {
        return config;
    }

    size_t GetZeroconfCount() const {
        return active_members.size();
    }

    std::pair<const char *, SocketAddress> Pick(sticky_hash_t sticky_hash);

private:
    void FillActive();

    /**
     * Pick the next active Zeroconf member in a round-robin way.
     * Does not update the #StickyCache.
     */
    const MemberMap::value_type &PickNextZeroconf();

    /**
     * Like PickNextZeroconf(), but skips members which are bad
     * according to failure_get_status().  If all are bad, a random
     * (bad) one is returned.
     */
    const MemberMap::value_type &PickNextGoodZeroconf();

    /* virtual methods from class AvahiServiceExplorerListener */
    void OnAvahiNewObject(const std::string &key,
                          SocketAddress address) override;
    void OnAvahiRemoveObject(const std::string &key) override;
};

#endif
