/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_CLUSTER_CONFIG_HXX
#define BENG_LB_CLUSTER_CONFIG_HXX

#include "SimpleHttpResponse.hxx"
#include "address_list.hxx"
#include "StickyMode.hxx"
#include "net/AllocatedSocketAddress.hxx"

#include <string>
#include <vector>
#include <forward_list>

struct LbMonitorConfig;

enum class LbProtocol {
    HTTP,
    TCP,
};

struct LbNodeConfig {
    std::string name;

    AllocatedSocketAddress address;

    /**
     * The Tomcat "jvmRoute" setting of this node.  It is used for
     * #StickyMode::JVM_ROUTE.
     */
    std::string jvm_route;

    explicit LbNodeConfig(const char *_name)
        :name(_name) {}

    LbNodeConfig(const char *_name, AllocatedSocketAddress &&_address)
        :name(_name), address(std::move(_address)) {}

    LbNodeConfig(LbNodeConfig &&src)
        :name(std::move(src.name)), address(std::move(src.address)),
         jvm_route(std::move(src.jvm_route)) {}
};

struct LbMemberConfig {
    const struct LbNodeConfig *node = nullptr;

    unsigned port = 0;
};

struct LbClusterConfig {
    std::string name;

    /**
     * The protocol that is spoken on this cluster.
     */
    LbProtocol protocol = LbProtocol::HTTP;

    /**
     * Use the client's source IP for the connection to the backend?
     * This is implemented using IP_TRANSPARENT and requires the
     * "tproxy" Linux kernel module.
     */
    bool transparent_source = false;

    bool mangle_via = false;

    LbSimpleHttpResponse fallback;

    StickyMode sticky_mode = StickyMode::NONE;

    std::string session_cookie = "beng_proxy_session";

    const LbMonitorConfig *monitor = nullptr;

    std::vector<LbMemberConfig> members;

    std::string zeroconf_service, zeroconf_domain;

    std::forward_list<AllocatedSocketAddress> address_allocations;

    /**
     * A list of node addresses.
     */
    AddressList address_list;

    explicit LbClusterConfig(const char *_name)
        :name(_name) {}

    LbClusterConfig(LbClusterConfig &&) = default;

    LbClusterConfig(const LbClusterConfig &) = delete;
    LbClusterConfig &operator=(const LbClusterConfig &) = delete;

    /**
     * Copy addresses of all members into the #AddressList.  This
     * needs to be called before using this instance.
     */
    void FillAddressList();

    /**
     * Returns the member index of the node with the specified
     * jvm_route value, or -1 if not found.
     */
    gcc_pure
    int FindJVMRoute(const char *jvm_route) const;

    bool HasZeroConf() const {
        return !zeroconf_service.empty();
    }
};

#endif
