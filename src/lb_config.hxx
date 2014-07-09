/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_CONFIG_H
#define BENG_LB_CONFIG_H

#include "address_list.hxx"
#include "sticky.h"
#include "ssl_config.hxx"

#include <http/status.h>

#include <glib.h>

#include <map>
#include <list>
#include <vector>
#include <string>

struct pool;

enum lb_protocol {
    LB_PROTOCOL_HTTP,
    LB_PROTOCOL_TCP,
};

struct lb_control_config {
    const struct address_envelope *envelope;

    lb_control_config():envelope(nullptr) {}
};

struct lb_monitor_config {
    std::string name;

    /**
     * Time in seconds between two monitor checks.
     */
    unsigned interval;

    /**
     * If the monitor does not produce a result after this timeout
     * [seconds], it is assumed to be negative.
     */
    unsigned timeout;

    enum class Type {
        NONE,
        PING,
        CONNECT,
        TCP_EXPECT,
    } type;

    /**
     * The timeout for establishing a connection.  Only applicable for
     * #Type::TCP_EXPECT.  0 means no special setting present.
     */
    unsigned connect_timeout;

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

    lb_monitor_config(const char *_name)
        :name(_name),
         interval(10), timeout(0),
         type(Type::NONE),
         connect_timeout(0) {}
};

struct lb_node_config {
    std::string name;

    const struct address_envelope *envelope;

    /**
     * The Tomcat "jvmRoute" setting of this node.  It is used for
     * #STICKY_JVM_ROUTE.
     */
    std::string jvm_route;

    lb_node_config(const char *_name,
                   const struct address_envelope *_envelope=nullptr)
        :name(_name),
         envelope(_envelope) {}
};

struct lb_member_config {
    const struct lb_node_config *node;

    unsigned port;

    lb_member_config():node(nullptr), port(0) {}
};

struct lb_fallback_config {
    http_status_t status;

    /**
     * The "Location" response header.
     */
    std::string location;

    std::string message;

    bool IsDefined() const {
        return !location.empty() || !message.empty();
    }
};

struct lb_cluster_config {
    std::string name;

    /**
     * The protocol that is spoken on this cluster.
     */
    enum lb_protocol protocol;

    /**
     * Use the client's source IP for the connection to the backend?
     * This is implemented using IP_TRANSPARENT and requires the
     * "tproxy" Linux kernel module.
     */
    bool transparent_source;

    bool mangle_via;

    struct lb_fallback_config fallback;

    enum sticky_mode sticky_mode;

    std::string session_cookie;

    const struct lb_monitor_config *monitor;

    std::vector<lb_member_config> members;

    /**
     * A list of node addresses.
     */
    struct address_list address_list;

    lb_cluster_config(const char *_name)
        :name(_name),
         protocol(LB_PROTOCOL_HTTP),
         transparent_source(false),
         mangle_via(false),
         sticky_mode(STICKY_NONE),
         session_cookie("beng_proxy_session"),
         monitor(nullptr) {}


    /**
     * Returns the member index of the node with the specified
     * jvm_route value, or -1 if not found.
     */
    gcc_pure
    int FindJVMRoute(const char *jvm_route) const;
};

struct lb_attribute_reference {
    enum class Type {
        METHOD,
        URI,
        HEADER,
    } type;

    std::string name;

    template<typename N>
    lb_attribute_reference(Type _type, N &&_name)
        :type(_type), name(std::forward<N>(_name)) {}
};

struct lb_branch_config;

struct lb_goto {
    const lb_cluster_config *cluster;
    const lb_branch_config *branch;

    lb_goto()
        :cluster(nullptr), branch(nullptr) {}

    lb_goto(lb_cluster_config *_cluster)
        :cluster(_cluster), branch(nullptr) {}

    lb_goto(lb_branch_config *_branch)
        :cluster(nullptr), branch(_branch) {}

    bool IsDefined() const {
        return cluster != nullptr || branch != nullptr;
    }

    gcc_pure
    lb_protocol GetProtocol() const;

    gcc_pure
    const char *GetName() const;
};

struct lb_condition_config {
    lb_attribute_reference attribute_reference;

    enum class Operator {
        EQUALS,
        REGEX,
    };

    Operator op;

    bool negate;

    std::string string;
    GRegex *regex;

    lb_condition_config(lb_attribute_reference &&a, bool _negate,
                        const char *_string)
        :attribute_reference(std::move(a)), op(Operator::EQUALS),
         negate(_negate), string(_string), regex(nullptr) {}

    lb_condition_config(lb_attribute_reference &&a, bool _negate,
                        GRegex *_regex)
        :attribute_reference(std::move(a)), op(Operator::REGEX),
         negate(_negate), regex(_regex) {}

    lb_condition_config(const lb_condition_config &other)
        :attribute_reference(other.attribute_reference),
         op(other.op), negate(other.negate),
         string(other.string),
         regex(other.op == Operator::REGEX
               ? g_regex_ref(other.regex)
               : nullptr) {}

    lb_condition_config(lb_condition_config &&other)
        :attribute_reference(std::move(other.attribute_reference)),
         op(other.op), negate(other.negate),
         string(std::move(other.string)),
         regex(other.regex) {
        other.regex = nullptr;
    }

    ~lb_condition_config() {
        if (regex != nullptr)
            g_regex_unref(regex);
    }

    gcc_pure
    bool Match(const char *value) const {
        switch (op) {
        case Operator::EQUALS:
            return (string == value) ^ negate;
            break;

        case Operator::REGEX:
            return g_regex_match(regex, value, GRegexMatchFlags(0),
                                 nullptr) ^ negate;
            break;
        }

        gcc_unreachable();
    }
};

struct lb_goto_if_config {
    lb_condition_config condition;

    lb_goto destination;

    lb_goto_if_config(lb_condition_config &&c, lb_goto d)
        :condition(std::move(c)), destination(d) {}
};

/**
 * An object that distributes connections or requests to the "real"
 * cluster.
 */
struct lb_branch_config {
    std::string name;

    lb_goto fallback;

    std::list<lb_goto_if_config> conditions;

    lb_branch_config(const char *_name)
        :name(_name) {}

    bool HasFallback() const {
        return fallback.IsDefined();
    }

    lb_protocol GetProtocol() const {
        return fallback.GetProtocol();
    }
};

inline lb_protocol
lb_goto::GetProtocol() const
{
    assert(IsDefined());

    return cluster != nullptr
        ? cluster->protocol
        : branch->GetProtocol();
}

inline const char *
lb_goto::GetName() const
{
    assert(IsDefined());

    return cluster != nullptr
        ? cluster->name.c_str()
        : branch->name.c_str();
}

struct lb_listener_config {
    std::string name;

    const struct address_envelope *envelope;

    lb_goto destination;

    bool verbose_response = false;

    bool ssl;

    struct ssl_config ssl_config;

    lb_listener_config(const char *_name)
        :name(_name),
         envelope(nullptr),
         ssl(false) {
    }
};

struct lb_config {
    std::list<lb_control_config> controls;

    std::map<std::string, lb_monitor_config> monitors;

    std::map<std::string, lb_node_config> nodes;

    std::map<std::string, lb_cluster_config> clusters;
    std::map<std::string, lb_branch_config> branches;

    std::list<lb_listener_config> listeners;

    template<typename T>
    gcc_pure
    const lb_monitor_config *FindMonitor(T &&t) const {
        const auto i = monitors.find(std::forward<T>(t));
        return i != monitors.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const lb_node_config *FindNode(T &&t) const {
        const auto i = nodes.find(std::forward<T>(t));
        return i != nodes.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const lb_cluster_config *FindCluster(T &&t) const {
        const auto i = clusters.find(std::forward<T>(t));
        return i != clusters.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const lb_goto FindGoto(T &&t) const {
        lb_goto g;

        g.cluster = FindCluster(t);
        if (g.cluster == nullptr)
            g.branch = FindBranch(std::forward<T>(t));

        return g;
    }

    template<typename T>
    gcc_pure
    const lb_branch_config *FindBranch(T &&t) const {
        const auto i = branches.find(std::forward<T>(t));
        return i != branches.end()
            ? &i->second
            : nullptr;
    }

    template<typename T>
    gcc_pure
    const lb_listener_config *FindListener(T &&t) const {
        for (const auto &i : listeners)
            if (i.name == t)
                return &i;

        return nullptr;
    }
};

G_GNUC_CONST
static inline GQuark
lb_config_quark(void)
{
    return g_quark_from_static_string("lb_config");
}

/**
 * Load and parse the specified configuration file.
 */
struct lb_config *
lb_config_load(struct pool *pool, const char *path, GError **error_r);

#endif
