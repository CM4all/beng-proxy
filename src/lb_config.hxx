/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_CONFIG_H
#define BENG_LB_CONFIG_H

#include "address_list.h"
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

struct lb_listener_config {
    std::string name;

    const struct address_envelope *envelope;

    const struct lb_cluster_config *cluster;

    bool ssl;

    struct ssl_config ssl_config;

    lb_listener_config(const char *_name)
        :name(_name),
         envelope(nullptr), cluster(nullptr),
         ssl(false) {
    }
};

struct lb_config {
    std::list<lb_control_config> controls;

    std::map<std::string, lb_monitor_config> monitors;

    std::map<std::string, lb_node_config> nodes;

    std::map<std::string, lb_cluster_config> clusters;

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
