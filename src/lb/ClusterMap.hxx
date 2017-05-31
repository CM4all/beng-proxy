/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_CLUSTER_MAP_HXX
#define BENG_LB_CLUSTER_MAP_HXX

#include "Cluster.hxx"
#include "util/StringLess.hxx"

#include <map>

class MyAvahiClient;
struct LbGotoConfig;
struct LbBranchConfig;
struct LbTranslationHandlerConfig;

class LbClusterMap {
    std::map<const char *, LbCluster, StringLess> clusters;

public:
    void Scan(const LbConfig &config, MyAvahiClient &avahi_client);
    void Scan(const LbGotoConfig &g, MyAvahiClient &avahi_client);

    LbCluster *Find(const char *name) {
        auto i = clusters.find(name);
        return i != clusters.end()
            ? &i->second
            : nullptr;
    }

    template<typename F>
    void ForEach(F &&f) {
        for (auto &i : clusters)
            f(i.second);
    }

private:
    void Scan(const LbTranslationHandlerConfig &config,
              MyAvahiClient &avahi_client);
    void Scan(const LbGotoIfConfig &config, MyAvahiClient &avahi_client);
    void Scan(const LbBranchConfig &config, MyAvahiClient &avahi_client);
    void Scan(const LbListenerConfig &config, MyAvahiClient &avahi_client);

    void Scan(const LbClusterConfig &config, MyAvahiClient &avahi_client);
};

#endif
