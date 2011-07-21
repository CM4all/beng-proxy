/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_CONFIG_H
#define BENG_LB_CONFIG_H

#include "address-list.h"
#include "sticky.h"
#include "ssl_config.h"

#include <inline/list.h>
#include <http/status.h>

#include <glib.h>

#include <stdbool.h>

struct pool;

enum {
    MAX_CLUSTER_MEMBERS = 64,
};

enum lb_protocol {
    LB_PROTOCOL_HTTP,
    LB_PROTOCOL_TCP,
};

struct lb_monitor_config {
    struct list_head siblings;

    const char *name;

    /**
     * Time in seconds between two monitor checks.
     */
    unsigned interval;

    enum {
        MONITOR_NONE,
        MONITOR_PING,
        MONITOR_CONNECT,
        MONITOR_TCP_EXPECT,
    } type;

    /**
     * For #MONITOR_TCP_EXPECT: a string that is sent to the peer
     * after the connection has been established.  May be NULL or
     * empty.
     */
    const char *send;

    /**
     * For #MONITOR_TCP_EXPECT: a string that is expected to be
     * received from the peer after the #send string has been sent.
     */
    const char *expect;
};

struct lb_node_config {
    struct list_head siblings;

    const char *name;

    const struct address_envelope *envelope;
};

struct lb_member_config {
    const struct lb_node_config *node;

    unsigned port;
};

struct lb_fallback_config {
    http_status_t status;

    /**
     * The "Location" response header.
     */
    const char *location;

    const char *message;
};

struct lb_cluster_config {
    struct list_head siblings;

    const char *name;

    /**
     * The protocol that is spoken on this cluster.
     */
    enum lb_protocol protocol;

    struct lb_fallback_config fallback;

    enum sticky_mode sticky_mode;

    const char *session_cookie;

    const struct lb_monitor_config *monitor;

    unsigned num_members;

    struct lb_member_config members[MAX_CLUSTER_MEMBERS];

    /**
     * A list of node addresses.
     */
    struct address_list address_list;
};

struct lb_listener_config {
    struct list_head siblings;

    const char *name;

    const struct address_envelope *envelope;

    const struct lb_cluster_config *cluster;

    bool ssl;

    struct ssl_config ssl_config;
};

struct lb_config {
    struct pool *pool;

    struct list_head monitors;

    struct list_head nodes;

    struct list_head clusters;

    struct list_head listeners;
};

G_GNUC_CONST
static inline GQuark
lb_config_quark(void)
{
    return g_quark_from_static_string("lb_config");
}

/**
 * Load and parse the specified configuration file.
 *
 * The function creates a new memory pool below the given one.
 * The structure is freed by calling pool_unref() on that pool.
 */
struct lb_config *
lb_config_load(struct pool *pool, const char *path,
               GError **error_r);

G_GNUC_PURE
const struct lb_monitor_config *
lb_config_find_monitor(const struct lb_config *config, const char *name);

G_GNUC_PURE
const struct lb_node_config *
lb_config_find_node(const struct lb_config *config, const char *name);

G_GNUC_PURE
const struct lb_cluster_config *
lb_config_find_cluster(const struct lb_config *config, const char *name);

G_GNUC_PURE
const struct lb_listener_config *
lb_config_find_listener(const struct lb_config *config, const char *name);

#endif
