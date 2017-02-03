/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_CONFIG_H
#define BENG_LB_CONFIG_H

#include "address_list.hxx"
#include "StickyMode.hxx"
#include "ssl/ssl_config.hxx"
#include "regex.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "certdb/Config.hxx"

#include <http/status.h>

#include <glib.h>

#include <map>
#include <list>
#include <forward_list>
#include <vector>
#include <string>

struct pool;
class Error;

enum class LbProtocol {
    HTTP,
    TCP,
};

struct LbControlConfig {
    AllocatedSocketAddress bind_address;
};

struct LbCertDatabaseConfig : CertDatabaseConfig {
    std::string name;

    /**
     * List of PEM path names containing certificator authorities
     * we're going to use to build the certificate chain.
     */
    std::list<std::string> ca_certs;

    explicit LbCertDatabaseConfig(const char *_name):name(_name) {}
};

struct LbMonitorConfig {
    std::string name;

    /**
     * Time in seconds between two monitor checks.
     */
    unsigned interval = 10;

    /**
     * If the monitor does not produce a result after this timeout
     * [seconds], it is assumed to be negative.
     */
    unsigned timeout = 0;

    enum class Type {
        NONE,
        PING,
        CONNECT,
        TCP_EXPECT,
    } type = Type::NONE;

    /**
     * The timeout for establishing a connection.  Only applicable for
     * #Type::TCP_EXPECT.  0 means no special setting present.
     */
    unsigned connect_timeout = 0;

    /**
     * For #Type::TCP_EXPECT: a string that is sent to the peer
     * after the connection has been established.  May be empty.
     */
    std::string send;

    /**
     * For #Type::TCP_EXPECT: a string that is expected to be
     * received from the peer after the #send string has been sent.
     */
    std::string expect;

    /**
     * For #Type::TCP_EXPECT: if that string is received from the
     * peer (instead of #expect), then the node is assumed to be
     * shutting down gracefully, and will only get sticky requests.
     */
    std::string fade_expect;

    explicit LbMonitorConfig(const char *_name)
        :name(_name) {}
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

struct LbSimpleHttpResponse {
    http_status_t status = http_status_t(0);

    /**
     * The "Location" response header.
     */
    std::string location;

    std::string message;

    LbSimpleHttpResponse() = default;
    explicit LbSimpleHttpResponse(http_status_t _status)
        :status(_status) {}

    bool IsDefined() const {
        return status != http_status_t(0);
    }
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
     * Returns the member index of the node with the specified
     * jvm_route value, or -1 if not found.
     */
    gcc_pure
    int FindJVMRoute(const char *jvm_route) const;

    bool HasZeroConf() const {
        return !zeroconf_service.empty();
    }
};

struct LbAttributeReference {
    enum class Type {
        METHOD,
        URI,
        HEADER,
    } type;

    std::string name;

    LbAttributeReference(Type _type)
        :type(_type) {}

    template<typename N>
    LbAttributeReference(Type _type, N &&_name)
        :type(_type), name(std::forward<N>(_name)) {}

    template<typename R>
    gcc_pure
    const char *GetRequestAttribute(const R &request) const {
        switch (type) {
        case Type::METHOD:
            return http_method_to_string(request.method);

        case Type::URI:
            return request.uri;

        case Type::HEADER:
            return request.headers.Get(name.c_str());
        }

        assert(false);
        gcc_unreachable();
    }

};

struct LbBranchConfig;

struct LbGoto {
    const LbClusterConfig *cluster = nullptr;
    const LbBranchConfig *branch = nullptr;
    LbSimpleHttpResponse response;

    LbGoto() = default;

    explicit LbGoto(LbClusterConfig *_cluster)
        :cluster(_cluster) {}

    explicit LbGoto(LbBranchConfig *_branch)
        :branch(_branch) {}

    explicit LbGoto(http_status_t _status)
        :response(_status) {}

    bool IsDefined() const {
        return cluster != nullptr || branch != nullptr ||
            response.IsDefined();
    }

    gcc_pure
    LbProtocol GetProtocol() const;

    gcc_pure
    const char *GetName() const;

    bool HasZeroConf() const;

    template<typename R>
    gcc_pure
    const LbGoto &FindRequestLeaf(const R &request) const;
};

struct LbConditionConfig {
    LbAttributeReference attribute_reference;

    enum class Operator {
        EQUALS,
        REGEX,
    };

    Operator op;

    bool negate;

    std::string string;
    UniqueRegex regex;

    LbConditionConfig(LbAttributeReference &&a, bool _negate,
                      const char *_string)
        :attribute_reference(std::move(a)), op(Operator::EQUALS),
         negate(_negate), string(_string) {}

    LbConditionConfig(LbAttributeReference &&a, bool _negate,
                      UniqueRegex &&_regex)
        :attribute_reference(std::move(a)), op(Operator::REGEX),
         negate(_negate), regex(std::move(_regex)) {}

    LbConditionConfig(LbConditionConfig &&other) = default;

    LbConditionConfig(const LbConditionConfig &) = delete;
    LbConditionConfig &operator=(const LbConditionConfig &) = delete;

    gcc_pure
    bool Match(const char *value) const {
        switch (op) {
        case Operator::EQUALS:
            return (string == value) ^ negate;

        case Operator::REGEX:
            return regex.Match(value) ^ negate;
        }

        gcc_unreachable();
    }

    template<typename R>
    gcc_pure
    bool MatchRequest(const R &request) const {
        const char *value = attribute_reference.GetRequestAttribute(request);
        if (value == nullptr)
            value = "";

        return Match(value);
    }
};

struct LbGotoIfConfig {
    LbConditionConfig condition;

    LbGoto destination;

    LbGotoIfConfig(LbConditionConfig &&c, LbGoto d)
        :condition(std::move(c)), destination(d) {}

    bool HasZeroConf() const {
        return destination.HasZeroConf();
    }
};

/**
 * An object that distributes connections or requests to the "real"
 * cluster.
 */
struct LbBranchConfig {
    std::string name;

    LbGoto fallback;

    std::list<LbGotoIfConfig> conditions;

    explicit LbBranchConfig(const char *_name)
        :name(_name) {}

    LbBranchConfig(LbBranchConfig &&) = default;

    LbBranchConfig(const LbBranchConfig &) = delete;
    LbBranchConfig &operator=(const LbBranchConfig &) = delete;

    bool HasFallback() const {
        return fallback.IsDefined();
    }

    LbProtocol GetProtocol() const {
        return fallback.GetProtocol();
    }

    bool HasZeroConf() const {
        if (fallback.HasZeroConf())
            return true;

        for (const auto &i : conditions)
            if (i.HasZeroConf())
                return true;

        return false;
    }

    template<typename R>
    gcc_pure
    const LbGoto &FindRequestLeaf(const R &request) const {
        for (const auto &i : conditions)
            if (i.condition.MatchRequest(request))
                return i.destination.FindRequestLeaf(request);

        return fallback.FindRequestLeaf(request);
    }
};

inline LbProtocol
LbGoto::GetProtocol() const
{
    assert(IsDefined());

    if (response.IsDefined())
        return LbProtocol::HTTP;

    return cluster != nullptr
        ? cluster->protocol
        : branch->GetProtocol();
}

inline const char *
LbGoto::GetName() const
{
    assert(IsDefined());

    return cluster != nullptr
        ? cluster->name.c_str()
        : branch->name.c_str();
}

inline bool
LbGoto::HasZeroConf() const
{
    return (cluster != nullptr && cluster->HasZeroConf()) ||
        (branch != nullptr && branch->HasZeroConf());
}

template<typename R>
const LbGoto &
LbGoto::FindRequestLeaf(const R &request) const
{
    if (branch != nullptr)
        return branch->FindRequestLeaf(request);

    return *this;
}

struct LbListenerConfig {
    std::string name;

    AllocatedSocketAddress bind_address;

    LbGoto destination;

    /**
     * If non-empty, sets SO_BINDTODEVICE.
     */
    std::string interface;

    bool reuse_port = false;

    bool verbose_response = false;

    bool ssl = false;

    SslConfig ssl_config;

    const LbCertDatabaseConfig *cert_db = nullptr;

    explicit LbListenerConfig(const char *_name)
        :name(_name) {}

    gcc_pure
    bool HasZeroConf() const {
        return destination.HasZeroConf();
    }
};

struct LbConfig {
    std::string access_logger;

    std::list<LbControlConfig> controls;

    std::map<std::string, LbCertDatabaseConfig> cert_dbs;

    std::map<std::string, LbMonitorConfig> monitors;

    std::map<std::string, LbNodeConfig> nodes;

    std::map<std::string, LbClusterConfig> clusters;
    std::map<std::string, LbBranchConfig> branches;

    std::list<LbListenerConfig> listeners;

    template<typename T>
    gcc_pure
    const LbMonitorConfig *FindMonitor(T &&t) const {
        const auto i = monitors.find(std::forward<T>(t));
        return i != monitors.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const LbCertDatabaseConfig *FindCertDb(T &&t) const {
        const auto i = cert_dbs.find(std::forward<T>(t));
        return i != cert_dbs.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const LbNodeConfig *FindNode(T &&t) const {
        const auto i = nodes.find(std::forward<T>(t));
        return i != nodes.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const LbClusterConfig *FindCluster(T &&t) const {
        const auto i = clusters.find(std::forward<T>(t));
        return i != clusters.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const LbGoto FindGoto(T &&t) const {
        LbGoto g;

        g.cluster = FindCluster(t);
        if (g.cluster == nullptr)
            g.branch = FindBranch(std::forward<T>(t));

        return g;
    }

    template<typename T>
    gcc_pure
    const LbBranchConfig *FindBranch(T &&t) const {
        const auto i = branches.find(std::forward<T>(t));
        return i != branches.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const LbListenerConfig *FindListener(T &&t) const {
        for (const auto &i : listeners)
            if (i.name == t)
                return &i;

        return nullptr;
    }

    bool HasCertDatabase() const {
        for (const auto &i : listeners)
            if (i.cert_db != nullptr)
                return true;

        return false;
    }

    gcc_pure
    bool HasZeroConf() const {
        for (const auto &i : listeners)
            if (i.HasZeroConf())
                return true;

        return false;
    }
};

/**
 * Load and parse the specified configuration file.  Throws an
 * exception on error.
 */
void
LoadConfigFile(LbConfig &config, const char *path);

#endif
